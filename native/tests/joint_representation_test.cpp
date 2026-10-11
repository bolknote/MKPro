#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/multirole_word_synthesis.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (const int code : codes)
    items.push_back(MachineItem::op(code, "joint representation fixture"));
}

void call(std::vector<MachineItem>& items, const std::string& label) {
  append(items, {0x53});
  items.push_back(MachineItem::address(label));
}

void finish(std::vector<MachineItem>& items) {
  append(items, {0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
}

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}

std::vector<std::string> observe(const std::vector<MachineItem>& items, bool encoded_mode = false) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "fixture must fit stock MK-61");
  for (const char* reg : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                          "a", "b", "c", "d", "e"})
    calc.set_register(reg, "7");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(12000, 8).stopped, "fixture must terminate");
  std::vector<std::string> result{calc.display_text(true)};
  calc.press(".");
  result.push_back(calc.display_text(true));
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4", "5",
                          "6", "7", "8", "9", "a", "b", "c", "d", "e"}) {
    if (encoded_mode && std::string(reg) == "8")
      continue; // The explicitly internal representation is allowed to change.
    result.push_back(calc.read_register(reg));
  }
  return result;
}

void word_solver_tests() {
  const std::vector<std::vector<std::string>> columns{{"1", "1"}};
  const std::vector<FiniteWordAddressDomain> domains{
      {"alpha", "8", {17}}, {"beta", "8", {29}}};
  const auto result = synthesize_multirole_words(columns, domains);
  require(!result.encodings.empty(), "data/address intersection must produce new words");
  require(result.encodings.front().words == std::vector<std::string>{"17", "29"},
          "words must be synthesized from address roles, not observed data seeds");
  for (const auto& encoding : result.encodings)
    require(certify_multirole_words(encoding, columns, domains), "independent certificate replay");
  auto corrupt = result.encodings.front();
  corrupt.words[0] = "18";
  require(!certify_multirole_words(corrupt, columns, domains), "wrong delivered address rejected");
  auto collision = domains;
  collision[1].targets = {17};
  require(synthesize_multirole_words(columns, collision).encodings.empty(),
          "different logical entries cannot alias one address");
  auto same = domains;
  same[1].identity = "alpha";
  require(synthesize_multirole_words(columns, same).encodings.empty(),
          "one logical entry cannot acquire two physical addresses");
  for (const char* reg : {"0", "4", "f"}) {
    auto unsupported = domains;
    unsupported[0].selector = reg;
    require(synthesize_multirole_words(columns, unsupported).encodings.empty(),
            "mutating selectors and Rf are outside the ordinary-word certificate");
  }
  const auto bounded = synthesize_multirole_words(columns, domains, {.maximum_work = 0});
  require(bounded.exhausted && bounded.encodings.empty(), "zero work budget is inconclusive");
  FiniteWordAddressDomain wide{"wide", "8", {}};
  for (int target = 1; target <= 20; ++target)
    wide.targets.push_back(target);
  const auto row_limited = synthesize_multirole_words(
      {{"1"}}, {wide}, {.maximum_work = 100000, .maximum_candidates = 100});
  require(row_limited.exhausted && !row_limited.encodings.empty() &&
          row_limited.work <= 100000U,
          "per-row candidate limits must also report an incomplete search");
  const auto repeated = synthesize_multirole_words(columns, domains);
  require(repeated.work == result.work &&
          repeated.encodings.front().words == result.encodings.front().words,
          "synthesis must be deterministic");
}

std::vector<MachineItem> region_fixture(bool different = false, bool branches = false) {
  std::vector<MachineItem> items;
  append(items, {0x4b, 7});
  call(items, "alpha");
  call(items, "beta");
  call(items, "alpha_entry");
  call(items, "beta_entry");
  finish(items);
  for (const std::string name : {"alpha", "beta"}) {
    items.push_back(MachineItem::label(name));
    append(items, {0x69});
    if (branches) {
      append(items, {0x57});
      items.push_back(MachineItem::address(name + "_arm"));
    }
    items.push_back(MachineItem::label(name + "_entry"));
    append(items, {different && name == "beta" ? 3 : 2, 0x10, 0x49, 0x52});
    if (branches) {
      items.push_back(MachineItem::label(name + "_arm"));
      append(items, {4, 0x10, 0x49, 0x52});
    }
  }
  return items;
}

void region_tests() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  for (const bool branches : {false, true}) {
    const auto original = region_fixture(false, branches);
    const auto result = passes::semantic_region_folding(raise_machine_to_ir(original), {options});
    const auto output = lower_ir_to_machine(result.ops);
    require(result.applied > 0 && cells(output) < cells(original), "multi-entry regions must fold");
    require(build_post_layout_control_flow(output).proved, "all entries and returns must resolve");
    require(observe(original) == observe(output), "region folding must preserve complete ROM state");
    std::cout << "region" << (branches ? "-cfg" : "") << ": " << cells(original)
              << " -> " << cells(output) << " cells\n";
  }
  auto different = region_fixture(true);
  require(passes::semantic_region_folding(raise_machine_to_ir(different), {options}).applied == 0,
          "different arithmetic effects must not be merged");
  auto numeric = region_fixture();
  for (auto& item : numeric)
    if (item.kind == MachineItemKind::Address) {
      item.target = 12;
      break;
    }
  require(passes::semantic_region_folding(raise_machine_to_ir(numeric), {options}).applied == 0,
          "physical operands require an independent retarget certificate");
  auto pinned = region_fixture();
  pinned[2] = MachineItem::op(0xa7, "physical selector");
  pinned[2].indirect_flow_targets = std::vector<IrTarget>{std::string{"beta"}};
  pinned.erase(pinned.begin() + 3);
  require(passes::semantic_region_folding(raise_machine_to_ir(pinned), {options}).applied == 0,
          "unchanged runtime selectors must retain their physical destinations");
}

std::vector<MachineItem> interactive_regions(bool mark_helpers = false) {
  std::vector<MachineItem> items;
  append(items, {0x6b, 0x50});
  items.back().stop_disposition = StopDisposition::Resumable;
  items.back().manual_interaction = ManualInteractionAnchor{
      7, -1, ManualInteractionAnchorKind::PromptStop};
  append(items, {0x40});
  items.back().manual_interaction = ManualInteractionAnchor{
      7, 0, ManualInteractionAnchorKind::SingleStepCommand};
  append(items, {0x41});
  items.back().manual_interaction = ManualInteractionAnchor{
      7, 1, ManualInteractionAnchorKind::ContinuousResume};
  call(items, "one");
  call(items, "two");
  finish(items);
  for (const std::string name : {"one", "two"}) {
    items.push_back(MachineItem::label(name));
    append(items, {0x69, 2, 0x10, 0x49, 0x52});
    if (mark_helpers)
      items[items.size() - 5U].manual_interaction = ManualInteractionAnchor{
          name == "one" ? 8 : 9, 0, ManualInteractionAnchorKind::SingleStepCommand};
  }
  return items;
}

std::vector<std::string> observe_interactive(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "interactive fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "interactive fixture must fit ROM");
  for (const char* reg : {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                          "a", "b", "c", "d", "e"})
    calc.set_register(reg, "7");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(12000, 8).stopped, "initial prompt must stop");
  std::vector<std::string> observations{calc.display_text(true), calc.program_counter()};
  calc.input_number("2");
  calc.press("ПП");
  require(calc.run_until_stable(12000, 8).stopped, "single-step input must stop");
  observations.push_back(calc.read_register("0"));
  observations.push_back(calc.program_counter());
  calc.input_number("3");
  calc.press("С/П");
  require(calc.run_until_stable(12000, 8).stopped, "continuous input must finish helpers");
  observations.push_back(calc.display_text(true));
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "0", "1", "2", "3", "4", "5",
                          "6", "7", "8", "9", "a", "b", "c", "d", "e"})
    observations.push_back(calc.read_register(reg));
  calc.press(".");
  observations.push_back(calc.display_text(true));
  return observations;
}

void interactive_region_tests() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto original = interactive_regions();
  require(build_post_layout_control_flow(original).proved,
          "fixture must have a complete manual protocol");
  const auto result = passes::semantic_region_folding(raise_machine_to_ir(original), {options});
  const auto output = lower_ir_to_machine(result.ops);
  require(result.applied > 0 && cells(output) == cells(original) - 5,
          "a manual caller must not block folding independent internal helpers");
  require(observe_interactive(original) == observe_interactive(output),
          "folding must preserve both input phases, full stack, last-X and entry shadow");
  const auto original_flow = build_post_layout_control_flow(original);
  const auto output_flow = build_post_layout_control_flow(output);
  require(output_flow.proved &&
              original_flow.external_entries.size() == output_flow.external_entries.size(),
          "folding must retain the complete external entry family");
  const auto protected_helpers = interactive_regions(true);
  require(passes::semantic_region_folding(raise_machine_to_ir(protected_helpers), {options}).applied == 0,
          "manual helper entries themselves must never become internal aliases");
  require(!passes::layout_symbolic_regions(raise_machine_to_ir(original), {{"one", 0}}, {options}),
          "layout must not displace the physical manual-entry component");

  // The public search must actually request web recoloring even when ordinary
  // copy deletion is disabled. These local lifetimes have identical effects
  // after allocation, but their source physical colors differ.
  std::vector<MachineItem> recolor;
  append(recolor, {0x4b, 2});
  call(recolor, "red");
  append(recolor, {0x43, 3});
  call(recolor, "blue");
  append(recolor, {0x43});
  finish(recolor);
  for (const std::string name : {"red", "blue"}) {
    recolor.push_back(MachineItem::label(name));
    const int reg = name == "red" ? 8 : 9;
    append(recolor, {0x40 + reg, 0x60 + reg, 2, 0x10, 0x52});
  }
  require(!options.coalesce_copies, "test must not enable the unrelated copy-deletion switch");
  const auto edges = expand_joint_representation_transactions({.items = recolor}, options);
  require(std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
            return edge.proved && edge.rule == "semantic-region-web-recolor";
          }), "joint search must not silently disable its allocator requests");
}

std::vector<MachineItem> mode_fixture(bool duplicate) {
  std::vector<MachineItem> items;
  append(items, {0x4b});
  for (int mode = 1; mode <= 8; ++mode) {
    append(items, {mode, 0});
    call(items, duplicate && mode % 2 == 0 ? "second" : "first");
  }
  finish(items);
  for (const std::string& name : duplicate ? std::vector<std::string>{"first", "second"}
                                         : std::vector<std::string>{"first"}) {
    items.push_back(MachineItem::label(name));
    append(items, {0x48, 0x68, 1, 0, 0x12, 0x49, 0x68, 1, 0, 0, 0x12, 0x4a, 0x52});
  }
  return items;
}

std::vector<MachineItem> address_fixture(bool expose = false) {
  std::vector<MachineItem> items;
  std::vector<std::size_t> literals;
  append(items, {0x4b});
  for (int mode = 0; mode < 2; ++mode) {
    literals.push_back(items.size());
    append(items, {1, 0, 0, 0});
    call(items, "worker");
  }
  if (expose)
    append(items, {0x68, 0x46}); // Bare mode observation forbids recoding.
  finish(items);
  items.push_back(MachineItem::label("worker"));
  append(items, {0x48, 0x68, 0x32, 0x49, 0xa8});
  items.back().indirect_flow_targets =
      std::vector<IrTarget>{std::string{"left"}, std::string{"right"}};
  append(items, {0x52});
  for (const std::string name : {"left", "right"}) {
    items.push_back(MachineItem::label(name));
    append(items, {0x69, name == "left" ? 2 : 3, 0x10, 0x49, 0x52});
  }
  std::map<std::string, int> labels;
  int address = 0;
  for (const auto& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels[item.name] = address;
    else
      ++address;
  }
  for (std::size_t row = 0; row < literals.size(); ++row) {
    const std::string value = std::to_string(1000 + labels.at(row == 0 ? "left" : "right"));
    for (std::size_t digit = 0; digit < value.size(); ++digit)
      items[literals[row] + digit].opcode = value[digit] - '0';
  }
  return items;
}

void representation_and_composition_tests() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const passes::PassContext context{options};
  const auto original = address_fixture();
  const auto candidates = passes::finite_mode_representation_candidates(
      raise_machine_to_ir(original), context,
      {.maximum_growth_cells = 2, .maximum_candidates = 16, .allow_target_relayout = true});
  const passes::PassResult* best = nullptr;
  for (const auto& candidate : candidates)
    if (!best || cells(lower_ir_to_machine(candidate.ops)) < cells(lower_ir_to_machine(best->ops)))
      best = &candidate;
  require(best && cells(lower_ir_to_machine(best->ops)) < cells(original),
          "data/address synthesis must repay changed literal geometry");
  const auto rewritten = lower_ir_to_machine(best->ops);
  require(observe(original, true) == observe(rewritten, true),
          "newly synthesized selectors must retain ROM destinations and hidden state");
  std::cout << "data-address: " << cells(original) << " -> " << cells(rewritten) << " cells\n";
  require(passes::finite_mode_representation_candidates(
              raise_machine_to_ir(address_fixture(true)), context,
              {.maximum_growth_cells = 2, .maximum_candidates = 16,
               .allow_target_relayout = true}).empty(),
          "an unprojected observation must reject every address representation");

  const auto combined = mode_fixture(true);
  const auto input = raise_machine_to_ir(combined);
  const auto just_regions = passes::semantic_region_folding(input, context);
  const auto just_words = passes::finite_mode_representation(input, context);
  const auto joint = passes::joint_representation_search(input, context);
  const auto result = lower_ir_to_machine(joint.ops);
  require(joint.applied > 0 &&
          cells(result) < cells(lower_ir_to_machine(just_regions.ops)) &&
          cells(result) < cells(lower_ir_to_machine(just_words.ops)),
          "composed search must beat either isolated local winner");
  require(observe(combined, true) == observe(result, true),
          "composed proofs must agree with ROM on stack/X1/X2 and results");
  std::cout << "joint: " << cells(combined) << " -> " << cells(result) << " cells\n";
}

LayoutSearchArtifact literal_selector_fixture(int repetitions, const std::string& reg,
                                             bool recall_entry = false) {
  LayoutSearchArtifact fixture;
  auto& items = fixture.items;
  append(items, {recall_entry ? 0x60 : 0x40});
  for (int i = 0; i < repetitions; ++i)
    append(items, {2, 0, 0x12, 0x61, 0x10});
  append(items, {0xa0 + static_cast<int>(std::string("0123456789abcde").find(reg))});
  items.back().indirect_flow_targets = std::vector<IrTarget>{std::string{"numeric_worker"}};
  call(items, "other_worker");
  append(items, {0x62, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  const int target = cells(items);
  items.push_back(MachineItem::label("numeric_worker"));
  append(items, {0x61, 2, 0x10, 0x44, 0x52});
  items.push_back(MachineItem::label("other_worker"));
  for (int i = 1; i < 14 - 4 * repetitions; ++i)
    append(items, {0x62});
  append(items, {0x52});
  fixture.preloads.push_back({.register_name = reg, .value = std::to_string(target),
      .lowered_data_value = "source-address-owner"});
  return fixture;
}

std::vector<std::string> observe_literal_selector(
    const LayoutSearchArtifact& artifact, const std::string& selector) {
  const auto resolved = resolve_machine_items(artifact.items);
  require(resolved.diagnostics.empty(), "literal/selector fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps) codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(), "literal/selector ROM load");
  for (const char reg : std::string("0123456789abcde"))
    calc.set_register(std::string(1, reg), "7");
  for (const auto& preload : artifact.preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.set_register("X", "7");
  calc.set_register("Y", "11");
  calc.set_register("Z", "13");
  calc.set_register("T", "17");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(12000, 8).stopped, "literal/selector ROM stop");
  std::vector<std::string> observation;
  for (const char* name : {"X", "Y", "Z", "T", "X1"})
    observation.push_back(calc.read_register(name));
  for (const char reg : std::string("0123456789abcde"))
    if (std::string(1, reg) != selector)
      observation.push_back(calc.read_register(std::string(1, reg)));
  calc.press(".");
  observation.push_back(calc.display_text(true));
  return observation;
}

bool used_literal_selector_equations(
    const TransactionSearchResult<LayoutSearchArtifact>& result) {
  return result.incumbent &&
      std::any_of(result.incumbent->optimizations.begin(), result.incumbent->optimizations.end(),
          [](const auto& report) { return report.name == "joint-literal-selector-co-synthesis"; });
}

void literal_selector_tests() {
  for (const std::string reg : {"8", "c", "e"})
    for (int repetitions = 1; repetitions <= 3; ++repetitions)
      for (const bool recalled : {false, true}) {
        const auto before = literal_selector_fixture(repetitions, reg, recalled);
        const auto result = search_joint_representation_constraints(before, {});
        require(used_literal_selector_equations(result),
                "joint search must synthesize a literal/address word and its new placement");
        require(cells(before.items) == 25 + repetitions &&
                    cells(result.incumbent->items) <= 25,
                "all literal copies must repay one joint layout, without added registers");
        require(result.incumbent->preloads.size() == before.preloads.size() &&
                    result.incumbent->preloads.front().lowered_data_value ==
                        before.preloads.front().lowered_data_value,
                "co-synthesis must preserve setup ownership and immutable data provenance");
        require(observe_literal_selector(before, reg) ==
                    observe_literal_selector(*result.incumbent, reg),
                "co-synthesis must preserve ROM results, stack, X1, X2, data and returns");
        const auto repeated = search_joint_representation_constraints(before, {});
        require(repeated.incumbent &&
                    same_layout_search_artifact(*result.incumbent, *repeated.incumbent),
                "joint word/layout choice must be deterministic");
      }

  const auto reject = [](LayoutSearchArtifact fixture, const char* message) {
    // Fixture edits retain the command identity but may move its initial PC.
    int address = 0;
    for (const auto& item : fixture.items) {
      if (item.kind == MachineItemKind::Label && item.name == "numeric_worker")
        fixture.preloads.front().value = std::to_string(address);
      if (item.kind != MachineItemKind::Label) ++address;
    }
    require(!used_literal_selector_equations(search_joint_representation_constraints(fixture, {})),
            message);
  };
  auto exposed = literal_selector_fixture(1, "c");
  exposed.items.insert(exposed.items.begin(),
      {MachineItem::op(0x6c, "observe selector"), MachineItem::op(0x45, "observable copy")});
  reject(std::move(exposed), "an observed preload must not acquire a different data value");
  auto shadow = literal_selector_fixture(1, "c");
  shadow.items[4] = MachineItem::op(0x0a, "observe X2 before synchronization");
  reject(std::move(shadow), "an observable entry shadow must block literal replacement");
  auto suppressed = literal_selector_fixture(1, "c");
  suppressed.items[0] = MachineItem::op(0x0e, "suppress next literal lift");
  reject(std::move(suppressed), "Enter suppression must not be mistaken for a fresh recall");
  auto raw = literal_selector_fixture(1, "c");
  raw.items[1].raw = true;
  reject(std::move(raw), "raw literals must retain their exact command sequence");
  auto written = literal_selector_fixture(1, "c");
  written.items[0] = MachineItem::op(0x4c, "mutable selector");
  reject(std::move(written), "a mutable selector must not become an immutable literal");
  auto generated = literal_selector_fixture(1, "c");
  generated.preloads.front().setup_expression = true;
  reject(std::move(generated), "computed setup must not be changed by rewriting its report");
  auto side = literal_selector_fixture(1, "c");
  side.items.insert(side.items.begin() + 2, MachineItem::label("literal_interior"));
  side.items.insert(side.items.begin(),
      {MachineItem::op(0x53, "side entry"), MachineItem::address(std::string{"literal_interior"})});
  reject(std::move(side), "an independent entry into the literal must reject pooling");
  const auto complete = literal_selector_fixture(1, "c");
  require(!used_literal_selector_equations(
              search_joint_representation_constraints(complete, {}, {}, {}, false)),
          "an incomplete preload environment cannot authorize word co-synthesis");
  require(!used_literal_selector_equations(search_joint_representation_constraints(
              complete, {}, {}, {.maximum_expansions = 0, .maximum_candidates = 0,
                                  .maximum_depth = 0, .maximum_growth_cells = 0})),
          "an exhausted search must not publish an unchecked literal equation");
  std::cout << "joint literal/address: 26/27/28 -> at most 25 cells; ROM and rejection contracts passed\n";
}

LayoutSearchArtifact deferred_erasure_fixture() {
  LayoutSearchArtifact fixture;
  auto& items = fixture.items;
  append(items, {0x40, 0xac});
  items.back().indirect_flow_targets = std::vector<IrTarget>{std::string{"worker"}};
  items.push_back(MachineItem::label("resume"));
  // The first recall is dead, but it is a live return destination. Its
  // deletion must retarget that frame to the retained recall, not drop it.
  append(items, {0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x10, 0x50});
  items.back().stop_disposition = StopDisposition::Terminal;
  const int worker = cells(items);
  items.push_back(MachineItem::label("worker"));
  append(items, {0x6a, 0x52});
  items.push_back(MachineItem::label("spare"));
  append(items, {0x52});
  for (int i = 0; i < 15; ++i)
    fixture.preloads.push_back({.register_name = std::string(1, "0123456789abcde"[i]),
        .value = std::to_string(i == 12 ? worker : i * 3 + 1)});
  return fixture;
}

bool contains_producer(const std::vector<MachineItem>& items) {
  return std::any_of(items.begin(), items.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 0x61;
  });
}

void deferred_stack_erasure_tests() {
  const CompileOptions options;
  const passes::PassContext context{options};
  const auto fixture = deferred_erasure_fixture();
  const auto& before = fixture.items;
  const std::size_t producer = 3;
  require(before[producer].opcode == 0x61, "fixture must identify its return destination");
  const std::map<std::string, int> origins{
      {"worker", std::stoi(fixture.preloads[12].value)}};
  const auto proposals = passes::deferred_stack_producer_erasure_candidates(
      raise_machine_to_ir(before), context, {}, origins);
  require(!proposals.empty(), "dead producers must supply conditional layout alternatives");
  std::optional<LayoutSearchArtifact> deleted;
  bool partial = false;
  bool combined = false;
  for (const auto& proposal : proposals) {
    auto after = fixture;
    after.items = lower_ir_to_machine(proposal.ops);
    const int saving = cells(before) - cells(after.items);
    partial = partial || saving == 1;
    combined = combined || saving == 2;
    if (saving == 1 && !contains_producer(after.items)) deleted = std::move(after);
  }
  require(partial && combined && deleted,
          "retain both partial erasures and their composition, not only the shortest form");
  // Keep the immutable selector's original number. The separate one-cell
  // component fills the hole before its target; deleting alone is not a
  // finished executable program and must not silently use a stale preload.
  const auto placed = passes::layout_symbolic_regions(
      raise_machine_to_ir(deleted->items), origins, context);
  require(placed && placed->changed, "erasure and immutable-selector placement must compose");
  deleted->items = lower_ir_to_machine(placed->ops);
  require(cells(deleted->items) == cells(before) - 1,
          "joint placement must not repay the saving with an extra instruction");
  require(observe_literal_selector(fixture, "") == observe_literal_selector(*deleted, ""),
          "erasure/layout must preserve ROM stack, X1/X2, every register and returns");

  auto neutral = before;
  neutral.erase(neutral.begin() + static_cast<std::ptrdiff_t>(producer));
  std::vector<std::optional<std::size_t>> relocation(before.size());
  for (std::size_t i = 0; i < before.size(); ++i)
    relocation[i] = i <= producer ? i : i - 1U;
  const auto flow = build_post_layout_control_flow(before);
  const auto next_flow = build_post_layout_control_flow(neutral);
  require(prove_post_layout_execution_relocation(
              before, neutral, flow, next_flow, relocation,
              {.erased_stack_producer_item = producer}).proved,
          "a pending return must follow the certified retained-command alias");
  require(!prove_post_layout_execution_relocation(
              before, neutral, flow, next_flow, relocation).proved,
          "ordinary relocation must not implicitly authorize command erasure");
  relocation[producer] = 0;
  require(!prove_post_layout_execution_relocation(
              before, neutral, flow, next_flow, relocation,
              {.erased_stack_producer_item = producer}).proved,
          "an incorrect erased-command alias must fail independent control transport");

  const auto reject = [&](const std::vector<MachineItem>& items,
                          const PostLayoutControlFlowOptions& control,
                          const char* message) {
    for (const auto& candidate : passes::deferred_stack_producer_erasure_candidates(
             raise_machine_to_ir(items), context, control, origins))
      require(contains_producer(lower_ir_to_machine(candidate.ops)), message);
  };
  auto raw = before;
  raw[producer].raw = true;
  reject(raw, {}, "raw producers must not disappear");
  auto owned = before;
  owned[producer].roles.push_back("exec");
  reject(owned, {}, "an executable address operand must not disappear");
  auto boundary = before;
  boundary[producer].manual_interaction.emplace();
  reject(boundary, {}, "manual-entry producers must not disappear");
  reject(before, {.main_entry = std::string{"resume"}},
         "an independent external entry must not become a continuation alias");
  auto shadow = before;
  shadow.insert(shadow.begin() + static_cast<std::ptrdiff_t>(producer + 2U),
      {MachineItem::op(0x0a, "observe entry shadow"),
       MachineItem::op(0x46, "publish observed shadow")});
  reject(shadow, {}, "an observable X2 difference must block producer erasure");
  auto live = before;
  live[producer + 2U] = MachineItem::op(0x50, "observe retained stack");
  live[producer + 2U].stop_disposition = StopDisposition::Terminal;
  reject(live, {}, "a live Y/Z/T at an untyped stop must block producer erasure");
  require(passes::deferred_stack_producer_erasure_candidates(
              raise_machine_to_ir(before), context, {}, origins,
              {.maximum_proof_attempts = 0}).empty(),
          "exhaustion must not export an unchecked erasure");
  require(passes::deferred_stack_producer_erasure_candidates(
              raise_machine_to_ir(before), context, {}, origins,
              {.maximum_candidates = 0}).empty(),
          "zero candidate budget must keep the original program");
  const auto repeated = passes::deferred_stack_producer_erasure_candidates(
      raise_machine_to_ir(before), context, {}, origins);
  require(repeated.size() == proposals.size(), "erasure frontier size must be deterministic");
  for (std::size_t i = 0; i < proposals.size(); ++i) {
    const auto first = lower_ir_to_machine(proposals[i].ops);
    const auto second = lower_ir_to_machine(repeated[i].ops);
    require(first.size() == second.size() &&
                std::equal(first.begin(), first.end(), second.begin(), machine_items_equal),
            "erasure subset ordering must be deterministic");
  }
  std::cout << "deferred stack erasure: data/CFG/return, layout, ROM and rejection contracts passed\n";
}

void layout_tests() {
  CompileOptions options;
  const passes::PassContext context{options};
  auto items = region_fixture();
  const auto ops = raise_machine_to_ir(items);
  const int alpha = 17;
  // Main occupies 17 cells: store, digit, four calls, seven-cell finalizer.
  const auto moved = passes::layout_symbolic_regions(
      ops, {{"beta", alpha}, {"alpha", alpha + 5}}, context);
  require(moved && moved->changed, "opaque labelled regions must be placeable in either order");
  require(observe(items) == observe(lower_ir_to_machine(moved->ops)),
          "permutation must preserve matched call/return contexts");
  require(!passes::layout_symbolic_regions(ops, {{"alpha", 1}}, context),
          "regions cannot overlap the external root");
  require(!passes::layout_symbolic_regions(ops, {{"alpha", 17}}, context, 0),
          "layout must fail closed on search exhaustion");
}

} // namespace

int main() {
  try {
    word_solver_tests();
    region_tests();
    interactive_region_tests();
    layout_tests();
    deferred_stack_erasure_tests();
    literal_selector_tests();
    representation_and_composition_tests();
    std::cout << "joint representation: compiler, constraints and ROM passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
