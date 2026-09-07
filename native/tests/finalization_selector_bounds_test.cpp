#include "mkpro/compiler.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
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
