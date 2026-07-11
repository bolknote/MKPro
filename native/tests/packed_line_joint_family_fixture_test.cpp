#include "mkpro/compiler.hpp"
#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

// Intentionally alpha-renamed: this is the smallest source fixture in which a
// four-bank mutating update walk and a four-bank score walk share the same bank
// and normalizer. It proves that the family lowerings recognize structure rather
// than source-level program, rule, or field names.
constexpr std::string_view kJointFamilySource = R"mkpro(
program QuartetKernelProbe {
  state {
    ledger: packed[4..7] = [44444.4, 44444.4, 44444.4, 44444.4]
    axis_p: counter 0..5 = 1
    axis_q: counter 0..5 = 2
    impulse: packed = 0
    aggregate: packed = 0
    band: packed = 0
    cursor: counter 0..8 = 4
  }

  fn apply_cell() {
    cursor--
    ledger[cursor] = packed_add(ledger[cursor], band, impulse)
    probe = bit_and(ledger[cursor], 88888834)
    if frac(probe) != 0 {
      halt(probe)
    }
  }

  fn sweep_bands(value) {
    impulse = value
    cursor = 8
    band = axis_p
    apply_cell()
    band = axis_q
    apply_cell()
    fold_band(axis_p + axis_q)
    apply_cell()
    fold_band(axis_p - axis_q)
    apply_cell()
  }

  fn rate_cell() {
    aggregate = packed_score(ledger[7], axis_p) + packed_score(ledger[6], axis_q)
    fold_band(axis_p + axis_q)
    aggregate += packed_score(ledger[5], band)
    fold_band(axis_p - axis_q)
    aggregate += packed_score(ledger[4], band)
  }

  fn fold_band(raw_band) {
    band = frac((raw_band + 3) / 4) * 4 + 1
  }

  loop {
    sweep_bands(1)
    rate_cell()
    halt(aggregate)
  }
}
)mkpro";

std::string trim_ascii(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    value.pop_back();
  return value;
}

struct Observation {
  std::string display;
  std::array<std::string, 4> banks;
};

Observation run_fixture(const CompileResult& result, std::string_view context) {
  require(
      result.implemented,
      std::string(context) + " should compile" +
          (result.diagnostics.empty() ? std::string() : ": " + result.diagnostics.front().message));
  require(result.diagnostics.empty(), std::string(context) + " should not report diagnostics");

  std::vector<int> codes;
  codes.reserve(result.steps.size());
  for (const ResolvedStep& step : result.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc;
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), std::string(context) + " should fit and load in MK-61");
  for (const PreloadReport& preload : result.preloads)
    calc.set_register(preload.register_name, preload.value);

  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(6000, 6);
  require(run.stopped, std::string(context) + " should halt with its aggregate score");

  return Observation{
      .display = trim_ascii(calc.display_text()),
      .banks = {trim_ascii(calc.read_register("4")), trim_ascii(calc.read_register("5")),
                trim_ascii(calc.read_register("6")), trim_ascii(calc.read_register("7"))},
  };
}

int joint_direct_normalizer_target(const CompileResult& result) {
  for (std::size_t index = 0; index + 1U < result.steps.size(); ++index) {
    const ResolvedStep& call = result.steps.at(index);
    if (call.opcode != 0x53 || !call.comment.has_value() ||
        !call.comment->starts_with("joint packed-line direct normalizer")) {
      continue;
    }
    return formal_address_info(
               result.steps.at(index + 1U).opcode,
               address_space_model_for_feature_profile(result.feature_profile))
        .actual;
  }
  require(false, "joint fixture should expose its direct normalizer call target");
  return -1;
}

} // namespace

void packed_line_joint_family_alpha_renamed_fixture_preserves_behavior() {
  CompileOptions baseline_options;
  baseline_options.budget = 999999;
  baseline_options.disable_candidate_search = true;

  CompileOptions family_options = baseline_options;
  family_options.canonicalize_packed_line_bank_walks = true;

  CompileOptions mutating_options = family_options;
  mutating_options.packed_line_family_mutating_selector_update_check_tail = true;

  CompileOptions joint_options = mutating_options;
  joint_options.stack_resident_temps = true;
  joint_options.joint_packed_line_family_walk = true;

  const CompileResult baseline = compile_source(std::string(kJointFamilySource), baseline_options);
  const CompileResult family = compile_source(std::string(kJointFamilySource), family_options);
  const CompileResult mutating = compile_source(std::string(kJointFamilySource), mutating_options);
  const CompileResult joint = compile_source(std::string(kJointFamilySource), joint_options);
  CompileOptions flow_probe_options = joint_options;
  flow_probe_options.dual_use_constant_indirect_flow = true;
  flow_probe_options.forward_indirect_flow = true;
  flow_probe_options.aggressive_post_layout_indirect_flow = true;
  const CompileResult flow_probe =
      compile_source(std::string(kJointFamilySource), flow_probe_options);
  CompileOptions fractional_options = flow_probe_options;
  fractional_options.dual_use_constant_indirect_flow = true;
  const int normalizer_target = joint_direct_normalizer_target(flow_probe);
  fractional_options.fractional_constant_selectors = {
      FractionalConstantSelectorPlan{.value = "0.41200076", .target = normalizer_target}};
  fractional_options.force_fractional_constant_selector_preloads = {"0.41200076"};
  const CompileResult fractional =
      compile_source(std::string(kJointFamilySource), fractional_options);

  require(std::any_of(mutating.optimizations.begin(), mutating.optimizations.end(),
                      [](const OptimizationReport& report) {
                        return report.name ==
                               "packed-line-family-mutating-selector-update-check-tail";
                      }),
          "alpha-renamed forced candidate should select the mutating packed-line family tail");
  require(std::any_of(joint.optimizations.begin(), joint.optimizations.end(),
                      [](const OptimizationReport& report) {
                        return report.name == "joint-packed-line-family-walk";
                      }),
          "alpha-renamed forced candidate should select the joint score/update walk");
  require(std::any_of(joint.optimizations.begin(), joint.optimizations.end(),
                      [](const OptimizationReport& report) {
                        return report.name == "joint-packed-line-shared-report-return";
                      }),
          "alpha-renamed joint candidate should share the score leaf return with the "
          "update/report leaf");
  require(optimizer_static_proof_gate_accepts_for_testing(joint_options, joint),
          "joint candidate should pass its final-artifact selector proof");
  const auto fractional_normalizer =
      std::find_if(fractional.steps.begin(), fractional.steps.end(),
                   [](const ResolvedStep& step) {
                     return step.comment.has_value() &&
                            step.comment->starts_with("joint packed-line direct normalizer") &&
                            (step.opcode & 0xf0) == 0xa0;
                   });
  require(fractional_normalizer != fractional.steps.end(),
          "joint fractional candidate should replace the two-cell normalizer call with К ПП");
  require(fractional.steps.size() == flow_probe.steps.size(),
          "alpha fixture should trade one direct-call address cell for one fractional recovery "
          "cell; a natural selector target saves the cell outright");
  std::string fractional_optimization_names;
  for (const OptimizationReport& report : fractional.optimizations)
    fractional_optimization_names += (fractional_optimization_names.empty() ? "" : ",") +
                                     report.name;
  require(std::any_of(fractional.optimizations.begin(), fractional.optimizations.end(),
                      [](const OptimizationReport& report) {
                        return report.name == "preloaded-indirect-flow" ||
                               report.name == "constants-dual-use" ||
                               report.name == "post-layout-existing-selector-flow";
                      }),
          "joint fractional candidate should report its proven indirect-flow rewrite: " +
              fractional_optimization_names);
  const std::optional<std::string> fractional_rejection =
      optimizer_static_proof_gate_rejection_reason_for_testing(fractional_options, fractional);
  require(fractional_rejection.has_value() &&
              fractional_rejection->find("requires a natural selector value") !=
                  std::string::npos,
          "alpha layout should fail closed because retargeting 0.41200076 requires a "
          "stack-affecting fractional recovery inside the shared score leaf");
  const auto fractional_preload =
      std::find_if(fractional.preloads.begin(), fractional.preloads.end(),
                   [](const PreloadReport& preload) {
                     return preload.value.find("41200076") != std::string::npos;
                   });
  require(fractional_preload != fractional.preloads.end(),
          "joint fractional proof fixture should expose its delivered selector preload");
  require(register_index(fractional_preload->register_name) ==
              (fractional_normalizer->opcode & 0x0f),
          "joint normalizer should call through the register carrying the fractional constant");
  CompileResult tampered_joint = joint;
  const auto dispatch = std::find_if(
      tampered_joint.items.begin(), tampered_joint.items.end(), [](const MachineItem& item) {
        return item.comment.has_value() &&
               item.comment->starts_with("joint-packed-line leaf dispatch;");
      });
  require(dispatch != tampered_joint.items.end(),
          "joint proof fixture should expose an indirect leaf dispatch");
  const int original_selector = dispatch->opcode & 0x0f;
  dispatch->opcode = (dispatch->opcode & 0xf0) | (original_selector == 7 ? 8 : 7);
  require(!optimizer_static_proof_gate_accepts_for_testing(joint_options, tampered_joint),
          "joint selector proof should reject a dispatch retargeted to another register");

  CompileResult tampered_report_return = joint;
  const auto report_return = std::find_if(
      tampered_report_return.items.begin(), tampered_report_return.items.end(),
      [](const MachineItem& item) {
        return item.kind == MachineItemKind::Op && item.comment.has_value() &&
               item.comment->starts_with("joint-packed-line-shared-report-return;");
      });
  require(report_return != tampered_report_return.items.end(),
          "joint proof fixture should expose the shared no-report return branch");
  report_return->opcode = 0x5e;
  report_return->mnemonic = "F x=0";
  require(!optimizer_static_proof_gate_accepts_for_testing(joint_options,
                                                            tampered_report_return),
          "joint final-artifact proof should reject a shared report-return branch with a "
          "tampered condition opcode");

  const Observation baseline_run = run_fixture(baseline, "alpha-renamed generic fixture");
  const Observation family_run = run_fixture(family, "alpha-renamed canonical family fixture");
  const Observation mutating_run =
      run_fixture(mutating, "alpha-renamed mutating-selector family fixture");
  const Observation joint_run =
      run_fixture(joint, "alpha-renamed joint score/update family fixture");
  require(family_run.display == baseline_run.display && family_run.banks == baseline_run.banks,
          "canonical family lowering should preserve the shared-bank behavior");
  require(mutating_run.display == baseline_run.display && mutating_run.banks == baseline_run.banks,
          "mutating-selector family lowering should preserve the alpha-renamed shared-bank "
          "behavior");
  require(joint_run.display == baseline_run.display && joint_run.banks == baseline_run.banks,
          "joint score/update family lowering should preserve the alpha-renamed shared-bank "
          "behavior: baseline=" + baseline_run.display + "/" + baseline_run.banks[0] + "," +
          baseline_run.banks[1] + "," + baseline_run.banks[2] + "," + baseline_run.banks[3] +
          " joint=" + joint_run.display + "/" + joint_run.banks[0] + "," +
          joint_run.banks[1] + "," + joint_run.banks[2] + "," + joint_run.banks[3]);
  require(family_run.display == "3,9073452-03" &&
              family_run.banks ==
                  std::array<std::string, 4>{"45444,4", "45444,4", "44544,4", "44454,4"},
          "fixture should add one mark to all four normalized bands and retain its aggregate "
          "score baseline: " +
              family_run.display + " | " + family_run.banks[0] + " " + family_run.banks[1] + " " +
              family_run.banks[2] + " " + family_run.banks[3]);
}

} // namespace mkpro::tests
