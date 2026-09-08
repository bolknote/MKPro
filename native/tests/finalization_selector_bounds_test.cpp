#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/passes/index.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mkpro::tests {
namespace {

struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  core::NaturalTargetComponentLayoutOptions options;
  std::size_t flexible_entry = 0;
  int flexible_address = 0;
  int fixed_address = 0;
};

Fixture reconciliation_fixture() {
  // Opaque helper identities and unrelated lengths exercise placement only.
  // A prior erasure has shifted the fixed selector's target back by one;
  // its unchanged preload must address that identity again in the final layout.
  constexpr std::array lengths{8, 18, 14, 17, 10, 4, 19};
  constexpr std::size_t fixed = 2;
  constexpr std::size_t flexible = 3;
  Fixture fixture;
  std::size_t fixed_call = 0;
  for (std::size_t helper = 0; helper < lengths.size(); ++helper) {
    const std::string label = "opaque_" + std::to_string(helper);
    if (helper == fixed || helper == flexible) {
      if (helper == fixed) fixed_call = fixture.items.size();
      MachineItem call = MachineItem::op(helper == fixed ? 0xac : 0xab, "call");
      call.indirect_flow_targets = std::vector<IrTarget>{label};
      fixture.items.push_back(std::move(call));
    } else {
      fixture.items.push_back(MachineItem::op(0x53, "call"));
      fixture.items.push_back(MachineItem::address(label));
    }
  }
  MachineItem stop = MachineItem::op(0x50, "stop");
  stop.stop_disposition = StopDisposition::Terminal;
  fixture.items.push_back(std::move(stop));
  int address = 13;
  for (std::size_t helper = 0; helper < lengths.size(); ++helper) {
    fixture.items.push_back(MachineItem::label("opaque_" + std::to_string(helper)));
    const std::size_t entry = fixture.items.size();
    if (helper == fixed) {
      fixture.fixed_address = address;
      fixture.options.required_absolute_targets.push_back({entry, address + 1});
      fixture.options.deferred_selector_reconciliations.push_back(
          {fixed_call, entry, address + 1});
    }
    if (helper == flexible) {
      fixture.flexible_entry = entry;
      fixture.flexible_address = address;
    }
    for (int cell = 1; cell < lengths.at(helper); ++cell)
      fixture.items.push_back(MachineItem::op(0x54, "nop"));
    fixture.items.push_back(MachineItem::op(0x52, "return"));
    address += lengths.at(helper);
  }
  fixture.preloads = {
      {.register_name = "b", .value = std::to_string(fixture.flexible_address)},
      {.register_name = "c", .value = std::to_string(fixture.fixed_address + 1)}};
  fixture.options.allow_size_neutral_absolute_layout = true;
  fixture.options.require_size_neutral_absolute_layout = true;
  return fixture;
}

core::NaturalTargetComponentLayoutResult place(const Fixture& fixture) {
  const auto control = core::build_post_layout_control_flow(fixture.items);
  require(control.proved, "selector-bound fixture must have authoritative control flow");
  return core::optimize_natural_target_component_layout(
      fixture.items, fixture.preloads, control, fixture.options);
}

std::vector<std::string> observe(const std::vector<MachineItem>& items,
                                  const std::vector<PreloadReport>& preloads) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "selector-bound fixture must resolve");
  std::vector<int> codes;
  for (const auto& step : resolved.steps) codes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(codes).diagnostics.empty(),
          "selector-bound fixture must fit physical program memory");
  for (const auto& preload : preloads)
    calc.set_register(preload.register_name, preload.value);
  calc.set_register("X", "7");
  calc.set_register("Y", "13");
  calc.set_register("Z", "23");
  calc.set_register("T", "37");
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(4000, 6).stopped,
          "selector-bound fixture must return through every helper to its stop");
  std::vector<std::string> result{calc.display_text(), calc.program_counter()};
  for (const std::string name : {"X", "Y", "Z", "T", "X1"})
    result.push_back(calc.read_register(name));
  // X2 is not an exposed register in the emulator API. Observe its keyboard
  // restoration as well as the separately addressable previous-X register.
  calc.press(".");
  result.push_back(calc.display_text());
  result.push_back(calc.read_register("X"));
  return result;
}

void finalization_formal_entry_contract() {
  const auto fixture = [](int address, int encoded) {
    std::vector<MachineItem> items{
        MachineItem::op(0x60, "recall"),
        MachineItem::op(0x41, "store"),
        MachineItem::op(0x51, "jump"),
        MachineItem::address(std::string("opaque_side_entry"))};
    items.back().formal_opcode = encoded;
    for (int cell = 4; cell < address; ++cell)
      items.push_back(MachineItem::op(0x54, "nop"));
    items.push_back(MachineItem::label("opaque_side_entry"));
    items.push_back(MachineItem::op(0x54, "nop"));
    MachineItem stop = MachineItem::op(0x50, "stop");
    stop.stop_disposition = StopDisposition::Terminal;
    items.push_back(std::move(stop));
    return items;
  };
  const auto codes = [](const std::vector<MachineItem>& items) {
    const auto resolved = resolve_machine_items(items);
    require(resolved.diagnostics.empty(), "formal-entry fixture must resolve");
    std::vector<int> result;
    for (const auto& step : resolved.steps)
      result.push_back(step.opcode);
    return result;
  };
  const auto stopped = [&](const std::vector<MachineItem>& items) {
    emulator::MK61 calc;
    require(calc.load_program(codes(items)).diagnostics.empty(),
            "formal-entry fixture must load");
    calc.set_register("0", "5");
    calc.press_sequence({"В/О", "С/П"});
    return calc.run_until_stable(500, 6).stopped;
  };

  const auto side = fixture(48, 0xfa);
  auto collapsed = side;
  collapsed.at(3).formal_opcode.reset();
  require(core::build_post_layout_control_flow(side).proved,
          "FA fixture must have an exact encoded-counter CFG");
  require(!stopped(side) && stopped(collapsed),
          "replacing FA by the same physical target changes the continuation");

  for (const auto pass : {core::passes::run_finalization_dead_store_elimination,
                          core::passes::run_finalization_redundant_literal_reload,
                          core::passes::run_post_inline_dead_store_elimination,
                          core::passes::run_post_inline_redundant_literal_reload}) {
    const auto rejected = pass(side, {});
    require(rejected.applied == 0 && rejected.removed_cell_addresses.empty() &&
                rejected.items.size() == side.size() &&
                rejected.items.at(3).formal_opcode == 0xfa &&
                codes(rejected.items) == codes(side),
            "physical-only IR cleanup must retain an explicit side-entry counter");

    auto indirect = side;
    indirect.at(2) = MachineItem::op(0x87, "indirect jump");
    indirect.at(2).indirect_flow_targets =
        std::vector<IrTarget>{std::string("opaque_side_entry")};
    indirect.at(2).indirect_flow_formal_targets = std::vector<int>{0xfa};
    indirect.at(3) = MachineItem::op(0x54, "nop");
    require(core::build_post_layout_control_flow(indirect).proved,
            "indirect FA fixture must retain its complete counter fact");
    const auto indirect_rejected = pass(indirect, {});
    require(indirect_rejected.applied == 0 &&
                indirect_rejected.items.at(2).indirect_flow_formal_targets ==
                    indirect.at(2).indirect_flow_formal_targets &&
                codes(indirect_rejected.items) == codes(indirect),
            "physical-only IR cleanup must not discard typed indirect aliases");

    auto invalid = side;
    invalid.at(3).formal_opcode = 256;
    const auto invalid_rejected = pass(invalid, {});
    require(invalid_rejected.applied == 0 &&
                invalid_rejected.items.at(3).formal_opcode == 256,
            "invalid encoded counters must fail closed without changing the artifact");
  }

  const auto ordinary = fixture(48, 0x48);
  const auto reduced = core::passes::run_finalization_dead_store_elimination(ordinary, {});
  require(reduced.applied == 1 &&
              codes(reduced.items).size() + 1U == codes(ordinary).size() &&
              stopped(reduced.items),
          "ordinary canonical flow must retain its dead-store saving");

  const auto expanded = fixture(105, 0xa5);
  CompileOptions expanded_options;
  expanded_options.feature_profile = FeatureProfile::Mk61SMiniExpanded;
  const auto expanded_reduced =
      core::passes::run_finalization_dead_store_elimination(expanded, expanded_options);
  require(expanded_reduced.applied == 1 &&
              expanded_reduced.items.size() + 1U == expanded.size(),
          "A5 is an ordinary physical cell in the 105+7 profile");
  expanded_options.optimizer_feature_profile_override = FeatureProfile::Standard;
  const auto stock_rejected =
      core::passes::run_finalization_dead_store_elimination(expanded, expanded_options);
  require(stock_rejected.applied == 0 &&
              stock_rejected.items.at(3).formal_opcode == 0xa5,
          "a stock-feature optimizer must not borrow expanded A5 addressing");
}

void discarded_selector_read_contract() {
  const auto fixture = [](int selector, bool short_branch, bool helper_call) {
    std::vector<MachineItem> items;
    auto append = [&](int code) { items.push_back(MachineItem::op(code, "opaque")); };
    append(0xd1);
    items.back().discarded_indirect_recall_value = true;
    append(0x60);
    append(0x5e);
    items.push_back(MachineItem::address(std::string("other")));
    if (helper_call) {
      append(0x53);
      items.push_back(MachineItem::address(std::string("flush")));
    } else {
      append(0x62); append(0x63); append(0x64);
    }
    append(0x51);
    items.push_back(MachineItem::address(std::string("join")));
    items.push_back(MachineItem::label("other"));
    append(0x62); append(0x63);
    if (!short_branch) append(0x64);
    items.push_back(MachineItem::label("join"));
    append(0x80 + selector);
    items.back().indirect_flow_targets = std::vector<IrTarget>{30};
    if (helper_call) {
      items.push_back(MachineItem::label("flush"));
      append(0x62); append(0x63); append(0x64); append(0x52);
    }
    int cells = static_cast<int>(std::count_if(items.begin(), items.end(),
        [](const MachineItem& item) { return item.kind != MachineItemKind::Label; }));
    while (cells++ < 30)
      append(0x54);
    append(0x50);
    items.back().stop_disposition = StopDisposition::Terminal;
    return items;
  };
  const auto rebound = [](const std::vector<MachineItem>& items,
                          const PreloadReport& preload) {
    const auto flow = core::build_post_layout_control_flow(items);
    require(flow.proved, "discarded-read fixture must have complete execution facts");
    return core::rebind_stable_preloaded_indirect_flow_selector(
        items, preload, flow, 30, 29);
  };
  for (const std::string name : {"7", "b", "e"}) {
    const int selector = std::stoi(name, nullptr, 16);
    for (bool calls : {false, true}) {
      const auto items = fixture(selector, false, calls);
      const std::vector<PreloadReport> preloads{
          {.register_name = name, .value = "30"},
          {.register_name = "1", .value = std::to_string(selector + 1)},
          {.register_name = "0", .value = "0"},
          {.register_name = "2", .value = "21"},
          {.register_name = "3", .value = "31"},
          {.register_name = "4", .value = "41"}};
      require(rebound(items, preloads.front()) == std::optional<std::string>{"29"},
              "all-path stack convergence must permit an address-only rebind");
      const auto flow = core::build_post_layout_control_flow(items);
      // The last padding cell is unreachable and precedes the moved target.
      const std::size_t erased_item = items.size() - 2U;
      const auto plan = core::plan_preloaded_indirect_flow_cell_erasure(
          items, preloads, flow, erased_item, 29, AddressSpaceModel::Standard);
      require(plan.proved && plan.preloads.front().value == "29",
              "nonobservation proof must compose with the complete erasure transaction");
      auto after = items;
      after.erase(after.begin() + static_cast<std::ptrdiff_t>(erased_item));
      for (auto& item : after)
        if (item.indirect_flow_targets.has_value())
          item.indirect_flow_targets = std::vector<IrTarget>{29};
      require(core::build_post_layout_control_flow(after).proved,
              "erased artifact must retain exact branch and return contexts");
      for (const std::string branch : {"0", "1"}) {
        auto before_preloads = preloads;
        auto after_preloads = plan.preloads;
        before_preloads.at(2).value = branch;
        after_preloads.at(2).value = branch;
        auto expected = observe(items, before_preloads);
        auto actual = observe(after, after_preloads);
        // Physical PC shifts with the erased cell; data observations must not.
        expected.erase(expected.begin() + 1);
        actual.erase(actual.begin() + 1);
        require(expected == actual,
                "both branch directions and helper returns must preserve stack, X1 and X2");
      }
    }

    const PreloadReport preload{.register_name = name, .value = "30"};
    require(!rebound(fixture(selector, true, false), preload),
            "one unflushed branch must reject the whole selector rewrite");

    auto stored = fixture(selector, false, false);
    stored.at(1) = MachineItem::op(0x46, "store");
    require(!rebound(stored, preload),
            "storing the differing value is an observation even if the stack is later flushed");

    auto last_x = fixture(selector, false, false);
    last_x.at(1) = MachineItem::op(0x25, "rotate");
    require(!rebound(last_x, preload),
            "stack rotation must not hide a differing physical last-X");

    auto exponent = fixture(selector, false, false);
    exponent.at(1) = MachineItem::op(0x0d, "clear");
    exponent.at(2) = MachineItem::op(0x0c, "exponent");
    exponent.at(3) = MachineItem::op(0x54, "nop");
    require(!rebound(exponent, preload),
            "numeric equality after clear must not authorize exposing unequal X2");

    auto enter = fixture(selector, false, false);
    enter.at(1) = MachineItem::op(0x0e, "enter");
    enter.at(2) = MachineItem::op(0x01, "digit");
    enter.at(3) = MachineItem::op(0x54, "nop");
    require(!rebound(enter, preload),
            "a digit after Enter must not invent a fresh stack lift");

    auto claimed = fixture(selector, true, false);
    claimed.front().indirect_memory_targets = std::vector<int>{selector};
    require(!rebound(claimed, preload),
            "an explicit alias set and dead-value flag cannot replace the machine-state proof");
  }
}

void require_not_rebindable(const Fixture& fixture, const std::string& context) {
  const auto control = core::build_post_layout_control_flow(fixture.items);
  require(control.proved, context + ": fixture control flow");
  require(!core::rebind_stable_preloaded_indirect_flow_selector(
              fixture.items, fixture.preloads.front(), control,
              fixture.flexible_address, 13, AddressSpaceModel::Standard),
          context + ": unsafe companion must not receive a retuning proof");
  const auto result = place(fixture);
  require(!result.plan.proved || result.plan.bounded_targets == 0,
          context + ": rejected retuning must not create a flexible bound");
  if (result.plan.proved) {
    const auto preload = std::find_if(result.preloads.begin(), result.preloads.end(),
                                     [](const PreloadReport& p) { return p.register_name == "b"; });
    require(preload != result.preloads.end() && preload->value == fixture.preloads.front().value,
            context + ": an accepted alternative must preserve the data preload");
  }
}

} // namespace

void finalization_selector_bounds_preserve_machine_contract() {
  finalization_formal_entry_contract();
  discarded_selector_read_contract();
  const Fixture fixture = reconciliation_fixture();
  const auto result = place(fixture);
  require(result.plan.proved && result.plan.final_artifact_proved &&
              result.plan.control_flow_equivalent && result.plan.call_return_equivalent &&
              result.plan.stack_and_x2_equivalent && result.plan.indirect_memory_equivalent &&
              result.plan.data_projection_equivalent && result.plan.absolute_targets_proved &&
              result.plan.deferred_selector_reconciliations_proved,
          "bounded reconciliation must retain every final-artifact proof");
  require(result.plan.input_cells == 103 && result.plan.output_cells == 103 &&
              result.plan.bounded_targets == 1 && result.plan.bounded_targets_proved,
          "a movable companion must get a proved decimal target bound without padding");
  require(!result.plan.runtime_selectors.empty(), "runtime selector proofs must be retained");
  auto original_preloads = fixture.preloads;
  // Before the modeled erasure, the same address-only call has its old target.
  // Neither preload is an observable data value in this synthetic program.
  original_preloads.back().value = std::to_string(fixture.fixed_address);
  require(observe(fixture.items, original_preloads) == observe(result.items, result.preloads),
          "relocation must preserve visible stop, stack, X2 and all caller returns");

  Fixture data = fixture;
  data.items.at(data.flexible_entry) = MachineItem::op(0x6b, "recall");
  require_not_rebindable(data, "ordinary data use");
  data.preloads.front().retunable_natural_fractional_prefix = "226000";
  require_not_rebindable(data, "uncertified data-use provenance");

  Fixture written = fixture;
  written.items.at(written.flexible_entry) = MachineItem::op(0x4b, "store");
  require_not_rebindable(written, "written selector");

  Fixture generated = fixture;
  generated.preloads.front().setup_expression = true;
  require_not_rebindable(generated, "generated setup value");

  Fixture unencodable = fixture;
  unencodable.options.required_absolute_targets.push_back({fixture.flexible_entry, 100});
  require(!place(unencodable).plan.proved,
          "an absolute placement outside the decimal selector domain must fail closed");

  for (const std::string selector_name : {"7", "b", "e"}) {
    const int selector = std::stoi(selector_name, nullptr, 16);
    std::vector<MachineItem> observed;
    MachineItem recall = MachineItem::op(0xd8, "indirect recall");
    recall.indirect_memory_targets = std::vector<int>{selector};
    observed.push_back(recall);
    MachineItem pause = MachineItem::op(0x50, "pause");
    pause.stop_disposition = StopDisposition::Resumable;
    observed.push_back(pause);
    MachineItem jump = MachineItem::op(0x80 + selector, "indirect jump");
    jump.indirect_flow_targets = std::vector<IrTarget>{20};
    observed.push_back(jump);
    while (observed.size() < 20U)
      observed.push_back(MachineItem::op(0x54, "nop"));
    MachineItem stop = MachineItem::op(0x50, "stop");
    stop.stop_disposition = StopDisposition::Terminal;
    observed.push_back(stop);
    const std::vector<PreloadReport> preloads{
        {.register_name = selector_name, .value = "20"},
        {.register_name = "8", .value = std::to_string(selector)},
        {.register_name = "0", .value = "31"}};
    const auto observed_flow = core::build_post_layout_control_flow(observed);
    require(observed_flow.proved, "indirect data observation must have complete control and memory facts");
    auto changed_preloads = preloads;
    changed_preloads.front().value = "19";
    require(observe(observed, preloads) != observe(observed, changed_preloads),
            "ROM must expose a selector changed through another register's indirect recall");
    require(!core::rebind_stable_preloaded_indirect_flow_selector(
                observed, preloads.front(), observed_flow, 20, 19),
            "indirect data targets must prevent address-only selector rebinding");
    require(!core::plan_preloaded_indirect_flow_cell_erasure(
                observed, preloads, observed_flow, 3, 3, AddressSpaceModel::Standard).proved,
            "a complete cell-erasure transaction must not retune an indirectly observed data word");
    require(core::rebind_stable_preloaded_indirect_flow_selector(
                observed, preloads.front(), observed_flow, 20, 20) == std::optional<std::string>{"20"},
            "an unchanged selector value remains legal even when indirectly observed");

    auto mixed = observed;
    mixed.front().indirect_memory_targets = std::vector<int>{0, selector};
    const auto mixed_flow = core::build_post_layout_control_flow(mixed);
    require(mixed_flow.proved && !core::rebind_stable_preloaded_indirect_flow_selector(
                mixed, preloads.front(), mixed_flow, 20, 19),
            "one potentially observing target in a wider alias set must suffice to reject");

    auto unknown = observed;
    unknown.front().indirect_memory_targets.reset();
    const auto unknown_flow = core::build_post_layout_control_flow(unknown);
    require(!unknown_flow.proved && !core::rebind_stable_preloaded_indirect_flow_selector(
                unknown, preloads.front(), unknown_flow, 20, 19),
            "unproved memory facts must never authorize a selector rewrite");
    require(!core::rebind_stable_preloaded_indirect_flow_selector(
                unknown, preloads.front(), observed_flow, 20, 19),
            "a stale caller fact must not hide missing memory targets on the actual command");

    auto fractional = observed;
    fractional.at(3) = MachineItem::op(0x60 + selector, "recall");
    fractional.at(4) = MachineItem::op(0x35, "fraction");
    auto fractional_preload = preloads.front();
    fractional_preload.value = "20.25";
    const auto fractional_flow = core::build_post_layout_control_flow(fractional);
    require(fractional_flow.proved && !core::rebind_stable_preloaded_indirect_flow_selector(
                fractional, fractional_preload, fractional_flow, 20, 19),
            "a direct fractional projection must not excuse a separate indirect whole-word observation");

    auto disjoint = observed;
    disjoint.front().indirect_memory_targets = std::vector<int>{0};
    auto disjoint_preloads = preloads;
    disjoint_preloads.at(1).value = "0";
    const auto disjoint_flow = core::build_post_layout_control_flow(disjoint);
    const auto erased = core::plan_preloaded_indirect_flow_cell_erasure(
        disjoint, disjoint_preloads, disjoint_flow, 3, 3, AddressSpaceModel::Standard);
    require(disjoint_flow.proved && erased.proved && erased.preloads.front().value == "19",
            "proved disjoint indirect reads must retain the profitable selector erasure");
    auto after_erasure = disjoint;
    after_erasure.erase(after_erasure.begin() + 3);
    after_erasure.at(2).indirect_flow_targets = std::vector<IrTarget>{19};
    require(core::build_post_layout_control_flow(after_erasure).proved &&
                observe(disjoint, disjoint_preloads) == observe(after_erasure, erased.preloads),
            "disjoint-read erasure must preserve ROM-visible stack, X1 and X2 observations");
  }
}

void compiler_explicit_variant_repeats_finalization_after_layout() {
  std::ifstream input("native/tests/fixtures/finalization-selector-release.mkpro");
  require(bool(input), "fixed selector-release regression source must be available");
  const std::string source((std::istreambuf_iterator<char>(input)), {});
  CompileOptions options;
  options.analysis = true;
  options.budget = 999999;
  options.disable_candidate_search = true;
  options.hoist_procs = true;
  options.proc_layout_strategy = "reverse";
  options.callee_hole_straight_line_helper = true;
  options.callee_hole_boundary_normalization = true;
  options.packed_score_accumulator_helpers = true;
  options.outline_single_use_packed_score_shared_tails = true;
  options.defer_return_suffix_until_callee_hole = true;
  options.x_param_value_functions = true;
  options.allow_size_neutral_selector_seed_reuse = true;
  options.suppress_constant_preloads.insert("-1");
  options.reserve_suppressed_constant_preload_slots.insert("-1");
  options.sign_normalized_x_param = true;
  const auto result = compile_source(source, options);
  require(result.implemented && result.steps.size() <= 129,
          "explicit lowering must revisit DSE after selector release and late layout");
  require(std::none_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == DiagnosticSeverity::Error; }),
          "repeated finalization must retain a valid published artifact");
}

} // namespace mkpro::tests
