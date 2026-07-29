#include "mkpro/core/compiler_static_proof_gate.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/core/super_dark_layout.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem digit() {
  return MachineItem::op(0x00, "0");
}

std::vector<MachineItem> jump(const std::string& target) {
  return {
      MachineItem::op(0x51, "БП"),
      MachineItem::address(target),
  };
}

std::vector<MachineItem> jump_address(int target) {
  return {
      MachineItem::op(0x51, "БП"),
      MachineItem::address(target),
  };
}

std::vector<MachineItem> call(const std::string& target) {
  return {
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(target),
  };
}

std::vector<MachineItem> cjump(const std::string& target) {
  return {
      MachineItem::op(0x5e, "F x=0"),
      MachineItem::address(target),
  };
}

std::vector<MachineItem> program_with_backward_jump(int filler_cells) {
  std::vector<MachineItem> items = {MachineItem::label("top")};
  for (int index = 0; index < filler_cells; ++index)
    items.push_back(digit());
  const std::vector<MachineItem> branch = jump("top");
  items.insert(items.end(), branch.begin(), branch.end());
  return items;
}

std::vector<MachineItem> super_dark_address_overlay_program() {
  std::vector<MachineItem> items = jump("site");
  items.push_back(MachineItem::label("continuation"));
  const std::vector<MachineItem> continuation = jump("done");
  items.insert(items.end(), continuation.begin(), continuation.end());
  for (int index = 0; index < 45; ++index) {
    if (index < 7) {
      const std::string name =
          index == 0 ? "8" : index == 1 ? "9"
                                          : std::string(1, static_cast<char>('a' + index - 2));
      items.push_back(MachineItem::op(0x48 + index, "X->П " + name));
    } else {
      items.push_back(MachineItem::op(0x0d, "Cx"));
    }
  }
  items.push_back(MachineItem::label("entry"));
  items.push_back(MachineItem::op(0x07, "7"));
  std::vector<MachineItem> return_to_continuation = jump("continuation");
  return_to_continuation.front().raw = true;
  items.insert(items.end(), return_to_continuation.begin(), return_to_continuation.end());
  items.push_back(MachineItem::label("site"));
  const std::vector<MachineItem> enter = jump("entry");
  items.insert(items.end(), enter.begin(), enter.end());
  items.push_back(MachineItem::label("done"));
  items.push_back(MachineItem::op(0x50, "С/П"));
  return items;
}

std::vector<MachineItem> error_padding_overlay_program() {
  std::vector<MachineItem> items = call("helper_return");
  MachineItem error = MachineItem::op(0x29, "К ÷");
  error.stop_disposition = StopDisposition::Resumable;
  items.push_back(std::move(error));
  MachineItem padding = MachineItem::op(0x54, "К НОП");
  padding.roles.push_back(kResumableErrorPaddingRole);
  items.push_back(std::move(padding));
  items.push_back(MachineItem::op(0x40, "X->П 0"));
  MachineItem stop = MachineItem::op(0x50, "С/П");
  stop.stop_disposition = StopDisposition::Terminal;
  items.push_back(std::move(stop));
  items.push_back(MachineItem::label("helper_return"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  return items;
}

std::vector<int> resolved_opcodes(const std::vector<MachineItem>& items) {
  const ResolvedProgram resolved = resolve_machine_items(items);
  std::vector<int> opcodes;
  opcodes.reserve(resolved.steps.size());
  for (const ResolvedStep& step : resolved.steps)
    opcodes.push_back(step.opcode);
  return opcodes;
}

} // namespace

void post_layout_indirect_flow_matches_typescript_contract() {
  CompileOptions options;
  options.delivery = DeliveryMode::Manual;
  options.budget = 999999;
  options.analysis = true;

  {
    const std::vector<MachineItem> program =
        error_padding_overlay_program();
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_error_padding_code_overlay(program);
    require(result.applied == 1 &&
                core::machine_cell_count(program) == 7 &&
                core::machine_cell_count(result.items) == 6,
            "error-padding/code overlay size baseline should be exactly 7->6 cells");
    require(!result.optimizations.empty() &&
                result.optimizations.front().name ==
                    "error-padding-code-overlay",
            "error-padding/code overlay should report its own optimization");
    const auto helper = std::find_if(
        result.items.begin(), result.items.end(),
        [](const MachineItem& item) {
          return item.kind == MachineItemKind::Label &&
                 item.name == "helper_return";
        });
    require(
        helper != result.items.end() && std::next(helper) != result.items.end() &&
            std::next(helper)->kind == MachineItemKind::Op &&
            std::next(helper)->opcode == 0x52 &&
            std::find(std::next(helper)->roles.begin(),
                      std::next(helper)->roles.end(),
                      kResumableErrorPaddingRole) !=
                std::next(helper)->roles.end(),
        "the separately addressed В/О should move onto the retained padding cell");

    for (const std::vector<MachineItem>* variant :
         {&program, &result.items}) {
      emulator::MK61 calc;
      calc.load_program(resolved_opcodes(*variant));
      calc.set_register("x", "5");
      calc.press("В/О");
      calc.press("С/П");
      const emulator::RunResult error_run =
          calc.run_until_stable(300, 5);
      require(error_run.stopped && calc.program_counter() == "04",
              "both layouts should call the helper return and stop at ЕГГ0Г with PC 04");
      calc.press("С/П");
      const emulator::RunResult resumed =
          calc.run_until_stable(300, 5);
      require(resumed.stopped && calc.read_register("0") == "5,",
              "continuing after ЕГГ0Г should skip the overlaid В/О and execute address 04");
    }

    std::vector<MachineItem> untyped_padding = program;
    untyped_padding.at(3).roles.clear();
    const core::PostLayoutIndirectFlowResult rejected_padding =
        core::optimize_post_layout_error_padding_code_overlay(
            untyped_padding);
    require(rejected_padding.applied == 0 &&
                core::machine_cell_count(rejected_padding.items) ==
                    core::machine_cell_count(untyped_padding),
            "an untyped ordinary cell after ЕГГ0Г must not be treated as padding");

    std::vector<MachineItem> fallthrough = program;
    fallthrough.at(5) = MachineItem::op(0x0d, "Cx");
    const core::PostLayoutIndirectFlowResult rejected_fallthrough =
        core::optimize_post_layout_error_padding_code_overlay(fallthrough);
    require(rejected_fallthrough.applied == 0,
            "a В/О entry with ordinary linear fallthrough must not move into error padding");
  }

  {
    CompileOptions borrowed_options = options;
    borrowed_options.aggressive_post_layout_indirect_flow = true;
    for (const std::string& register_name : {"7", "8", "a", "b", "c", "d", "e"})
      borrowed_options.preloaded_constant_registers[register_name] = "99";

    std::vector<MachineItem> program = {MachineItem::label("main")};
    const std::vector<MachineItem> branch = cjump("branch_write");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::op(0x49, "X->П 9"));
    MachineItem fallthrough_stop = MachineItem::op(0x50, "С/П");
    fallthrough_stop.stop_disposition = StopDisposition::Terminal;
    program.push_back(std::move(fallthrough_stop));
    program.push_back(MachineItem::label("branch_write"));
    program.push_back(MachineItem::op(0x49, "X->П 9"));
    MachineItem branch_stop = MachineItem::op(0x50, "С/П");
    branch_stop.stop_disposition = StopDisposition::Terminal;
    program.push_back(std::move(branch_stop));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, borrowed_options, 0);
    require(result.applied == 1 &&
                core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "post-layout flow should borrow R9 before its first write when every path proves "
            "the entry value dead");
    require(result.preloads.size() == 1 && result.preloads.front().register_name == "9",
            "entry-phase rewrite should preload the allocated R9 itself");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const auto& optimization) {
                          return optimization.name == "borrowed-entry-phase-selector";
                        }),
            "entry-phase register borrowing should be reported explicitly");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) { return item.borrowed_entry_phase_selector; }),
            "borrowed selector use should retain typed proof provenance in the final artifact");
    require(core::prove_post_layout_borrowed_entry_selectors(result.items).proved,
            "the final borrowed-selector artifact should pass the exact lifetime proof");

    CompileResult verified;
    verified.items = result.items;
    verified.steps = resolve_machine_items(result.items, borrowed_options).steps;
    verified.preloads = result.preloads;
    verified.registers["allocated_value"] = "9";
    for (const auto& optimization : result.optimizations) {
      verified.optimizations.push_back(OptimizationReport{
          .name = optimization.name,
          .detail = optimization.detail,
      });
    }
    require(optimizer_static_proof_gate_accepts_for_testing(borrowed_options, verified),
            "the optimizer acceptance boundary should accept a typed, exact borrowed lifetime "
            "even though R9 is allocated as ordinary data");
  }

  {
    CompileOptions recurring_options = options;
    recurring_options.aggressive_post_layout_indirect_flow = true;
    for (const std::string& register_name : {"7", "8", "a", "b", "c", "d", "e"})
      recurring_options.preloaded_constant_registers[register_name] = "99";

    std::vector<MachineItem> program = {MachineItem::label("main")};
    const std::vector<MachineItem> branch = cjump("write");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("write"));
    program.push_back(MachineItem::op(0x49, "X->П 9"));
    const std::vector<MachineItem> loop = jump("main");
    program.insert(program.end(), loop.begin(), loop.end());

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, recurring_options, 0);
    require(result.applied == 0 &&
                core::machine_cell_count(result.items) == core::machine_cell_count(program),
            "entry-phase R9 borrowing must fail when control loops back after R9 is overwritten");
    require(std::none_of(result.optimizations.begin(), result.optimizations.end(),
                         [](const auto& optimization) {
                           return optimization.name == "borrowed-entry-phase-selector";
                         }),
            "a recurring branch must not claim the entry-phase lifetime proof");
  }

  {
    const std::vector<MachineItem> program = program_with_backward_jump(110);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, options);

    require(result.applied == 1,
            "post-layout indirect flow should rescue an over-limit backward jump");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "post-layout indirect flow should shrink the direct branch by one cell");
    require(result.preloads.size() == 1,
            "post-layout indirect flow should report one selector preload");
    require(!result.preloads.at(0).counts_against_program,
            "post-layout selector preload should not count against program cells");

    const bool has_indirect_jump =
        std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode >= 0x80 && item.opcode <= 0x8e;
        });
    require(has_indirect_jump, "post-layout indirect flow should emit an indirect jump");
    const bool has_direct_jump_or_address =
        std::any_of(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return (item.kind == MachineItemKind::Op && item.opcode == 0x51) ||
                 item.kind == MachineItemKind::Address;
        });
    require(!has_direct_jump_or_address,
            "post-layout indirect flow should remove the direct jump/address pair");

    const std::optional<core::IndirectAddressEvaluation> decoded = core::evaluate_indirect_address(
        result.preloads.at(0).register_name, result.preloads.at(0).value,
        core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target.has_value() &&
                *decoded->actual_flow_target == 0,
            "post-layout selector preload should decode back to the final target address");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "dark-entry-layout";
                        }),
            "post-layout indirect flow should report dark-entry layout for proven formal targets");
  }

  {
    const std::vector<MachineItem> program = program_with_backward_jump(10);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, options);

    require(result.applied == 0, "post-layout indirect flow should leave in-budget code alone");
    require(result.preloads.empty(),
            "in-budget post-layout indirect flow should not add selector preloads");
    require(result.items.size() == program.size(),
            "in-budget post-layout indirect flow should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "in-budget post-layout indirect flow should be byte-identical");
    }
  }

  {
    const std::vector<MachineItem> program = program_with_backward_jump(104);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, options);

    require(core::machine_cell_count(result.items) <= core::machine_cell_count(program),
            "post-layout indirect flow should never grow a program");
    require(core::machine_cell_count(result.items) <= 105,
            "post-layout indirect flow should stop once the official window is reached");
  }

  {
    const std::vector<MachineItem> program = super_dark_address_overlay_program();
    const core::PostLayoutIndirectFlowResult baseline =
        core::optimize_post_layout_indirect_flow(program, options, 0);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_super_dark_address_overlay(program, options, 0);

    require(core::machine_cell_count(program) == 55 &&
                core::machine_cell_count(baseline.items) == 54 &&
                core::machine_cell_count(result.items) == 53,
            "joint super-dark/address overlay fixture should measure 55 -> 54 -> 53 cells");
    require(core::machine_cell_count(result.items) < core::machine_cell_count(baseline.items),
            "joint super-dark/address overlay should beat indirect flow alone");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "address-code-overlay";
                        }),
            "joint super-dark/address overlay should report the address-byte packing");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "preloaded-super-dark-flow";
                        }),
            "joint super-dark/address overlay should select FA..FF dispatch");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "super-dark-address-code-overlay";
                        }),
            "joint super-dark/address overlay should report the combined tactic");

    std::map<std::string, int> label_addresses;
    int address = 0;
    for (const MachineItem& item : result.items) {
      if (item.kind == MachineItemKind::Label)
        label_addresses[item.name] = address;
      else
        ++address;
    }

    address = 0;
    bool continuation_on_address_operand = false;
    for (std::size_t index = 0; index < result.items.size(); ++index) {
      const MachineItem& item = result.items.at(index);
      if (item.kind == MachineItemKind::Label) {
        if (item.name == "continuation" && index + 1U < result.items.size()) {
          const MachineItem& next = result.items.at(index + 1U);
          const auto* target = std::get_if<std::string>(&next.target);
          const bool physical_jump_opcode =
              (next.formal_opcode.has_value() && *next.formal_opcode == 0x51) ||
              (target != nullptr && label_addresses.contains(*target) &&
               label_addresses.at(*target) == 51);
          continuation_on_address_operand =
              address == 1 && next.kind == MachineItemKind::Address &&
              physical_jump_opcode &&
              std::find(next.roles.begin(), next.roles.end(), "exec") != next.roles.end();
        }
        continue;
      }
      ++address;
    }
    require(continuation_on_address_operand,
            "FA continuation 01 should execute the direct-jump operand as code");

    std::map<std::string, std::string> selector_values;
    for (const PreloadReport& preload : result.preloads)
      selector_values[preload.register_name] = preload.value;
    const LowerLayoutResult lowered =
        lower_ir_to_layout(raise_machine_to_ir(result.items));
    const SuperDarkLayoutProof proof = verify_super_dark_suffix_layout(
        lowered.cells, {.selector_values = std::move(selector_values)});
    require(proof.proved,
            "joint super-dark/address overlay should pass the final physical-layout proof");
    require(std::any_of(proof.pairs.begin(), proof.pairs.end(),
                        [](const SuperDarkLayoutPair& pair) {
                          return pair.formal == 0xfa && pair.continuation_address == 1;
                        }),
            "final FA proof should identify the overlaid address byte as continuation 01");
  }

  {
    // Without the protected direct continuation and with another free selector,
    // indirect flow can shift the overlaid target from 51 to 50.  That trial is
    // smaller, but the address byte would no longer execute the overlaid БП.
    std::vector<MachineItem> program = super_dark_address_overlay_program();
    for (MachineItem& item : program) {
      if (item.kind == MachineItemKind::Op && item.opcode == 0x51 && item.raw)
        item.raw = false;
      if (item.kind == MachineItemKind::Op && item.opcode == 0x48)
        item = MachineItem::op(0x0d, "Cx");
    }
    const core::PostLayoutIndirectFlowResult baseline =
        core::optimize_post_layout_indirect_flow(program, options, 0);
    const core::PostLayoutIndirectFlowResult overlay =
        core::optimize_post_layout_address_code_overlay(program, {}, options);
    const core::PostLayoutIndirectFlowResult unverified_trial =
        core::optimize_post_layout_indirect_flow(overlay.items, options, 0);
    require(core::machine_cell_count(unverified_trial.items) <
                core::machine_cell_count(baseline.items),
            "shifted-opcode trial should be tempting on size before final verification");

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_super_dark_address_overlay(program, options, 0);
    require(core::machine_cell_count(result.items) ==
                core::machine_cell_count(baseline.items),
            "joint packing should reject an address byte whose final opcode changed");
    require(std::none_of(result.optimizations.begin(), result.optimizations.end(),
                         [](const core::passes::AppliedOptimization& optimization) {
                           return optimization.name == "super-dark-address-code-overlay";
                         }),
            "rejected shifted-opcode trial must not report joint packing");
  }

  {
    std::vector<MachineItem> program = {MachineItem::label("main")};
    const std::vector<MachineItem> branch = jump("skip");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(digit());
    program.push_back(MachineItem::label("skip"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, options, 0);

    require(result.applied == 1,
            "post-layout indirect flow should prove shifted forward label targets");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "shifted forward post-layout rewrite should shrink by one cell");
    require(result.preloads.size() == 1 && result.preloads.at(0).register_name == "7" &&
                result.preloads.at(0).value == "B4",
            "shifted forward label target should preload R7=B4");
    const std::optional<core::IndirectAddressEvaluation> decoded =
        core::evaluate_indirect_address("7", "B4", core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target == 2,
            "shifted forward selector should decode to final address 2");
    const bool has_address =
        std::any_of(result.items.begin(), result.items.end(),
                    [](const MachineItem& item) { return item.kind == MachineItemKind::Address; });
    require(!has_address, "shifted forward post-layout rewrite should remove address cells");
  }

  {
    std::vector<MachineItem> program = {MachineItem::label("main")};
    const std::vector<MachineItem> branch = jump_address(3);
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, options, 0);

    require(result.applied == 1,
            "post-layout indirect flow should recover labels for numeric forward targets");
    require(result.preloads.size() == 1 && result.preloads.at(0).register_name == "7" &&
                result.preloads.at(0).value == "B4",
            "numeric forward target should use the same shifted selector preload");
    const std::optional<core::IndirectAddressEvaluation> decoded =
        core::evaluate_indirect_address("7", "B4", core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target == 2,
            "numeric shifted selector should decode to final address 2");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> direct_call = call("fractional_target");
    program.insert(program.end(), direct_call.begin(), direct_call.end());
    for (int index = 0; index < 75; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("fractional_target"));
    program.push_back(MachineItem::op(0x52, "В/О"));

    CompileOptions fixed_point_options = options;
    fixed_point_options.dual_use_constant_indirect_flow = true;
    fixed_point_options.forward_indirect_flow = true;
    for (const std::string& register_name : {"7", "8", "9", "a", "b", "c", "d"})
      fixed_point_options.preloaded_constant_registers[register_name] = "0";
    fixed_point_options.preloaded_constant_registers["e"] = "4.1200076E-1";

    const core::PostLayoutIndirectFlowResult fixed_point =
        core::optimize_post_layout_indirect_flow(program, fixed_point_options, 0);
    require(fixed_point.applied == 1,
            "post-layout dual-use flow should solve an existing fractional selector against "
            "the target address after deleting the direct call operand");
    require(core::machine_cell_count(fixed_point.items) == core::machine_cell_count(program) - 1,
            "proved fractional fixed-point call should save one program cell");
    require(fixed_point.preloads.empty(),
            "fractional fixed-point call should reuse the delivered constant preload");
    const auto indirect_call = std::find_if(
        fixed_point.items.begin(), fixed_point.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0xae;
        });
    require(indirect_call != fixed_point.items.end() && indirect_call->comment.has_value() &&
                indirect_call->comment->find("indirect-target=76") != std::string::npos,
            "natural 0.41200076 selector should become one-cell К ПП e to final address 76");
    require(std::any_of(fixed_point.optimizations.begin(), fixed_point.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "constants-dual-use";
                        }),
            "fractional fixed-point call should report existing-constant dual use");

    CompileOptions mismatched_options = fixed_point_options;
    mismatched_options.preloaded_constant_registers["e"] = "4.1200075E-1";
    const core::PostLayoutIndirectFlowResult mismatched =
        core::optimize_post_layout_indirect_flow(program, mismatched_options, 0);
    require(mismatched.applied == 0 &&
                core::machine_cell_count(mismatched.items) == core::machine_cell_count(program),
            "fractional fixed-point call should fail closed when the delivered selector misses "
            "the post-deletion target");

    std::vector<MachineItem> overwritten_program = program;
    overwritten_program.at(2) = MachineItem::op(0x4e, "П→x e");
    const core::PostLayoutIndirectFlowResult overwritten =
        core::optimize_post_layout_indirect_flow(overwritten_program, fixed_point_options, 0);
    require(overwritten.applied == 0 && core::machine_cell_count(overwritten.items) ==
                                            core::machine_cell_count(overwritten_program),
            "fractional fixed-point call should fail closed when program code can overwrite "
            "the preloaded selector register");
  }

  {
    CompileOptions expanded_options = options;
    expanded_options.feature_profile = FeatureProfile::Mk61SMiniExpanded;
    std::vector<MachineItem> program = {MachineItem::label("main")};
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    for (int index = 0; index < 104; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, expanded_options, 0);

    require(result.applied == 1,
            "expanded post-layout indirect flow should rewrite a forward target at 105");
    require(result.preloads.size() == 1 && result.preloads.at(0).value == "A5",
            "expanded post-layout selector for final target 105 should be A5");
    const std::optional<core::IndirectAddressEvaluation> decoded = core::evaluate_indirect_address(
        result.preloads.at(0).register_name, result.preloads.at(0).value,
        core::IndirectOperationKind::Flow, AddressSpaceModel::Mk61SMiniExpanded);
    require(decoded.has_value() && decoded->actual_flow_target == 105,
            "expanded A5 selector should decode to official address 105");
  }

  {
    std::vector<MachineItem> program = {MachineItem::label("main")};
    const std::vector<MachineItem> first = cjump("end");
    program.insert(program.end(), first.begin(), first.end());
    program.push_back(digit());
    const std::vector<MachineItem> second = cjump("end");
    program.insert(program.end(), second.begin(), second.end());
    program.push_back(digit());
    program.push_back(MachineItem::label("end"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_indirect_flow(program, options, 0);

    require(result.applied == 2,
            "post-layout indirect flow should group repeated forward branches");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 2,
            "grouped shifted forward rewrite should remove two address cells");
    require(result.preloads.size() == 1 && result.preloads.at(0).register_name == "7" &&
                result.preloads.at(0).value == "B6",
            "grouped shifted forward target should preload R7=B6");
    const int indirect_conditions = static_cast<int>(
        std::count_if(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0xe7;
        }));
    require(indirect_conditions == 2,
            "grouped shifted forward rewrite should emit two indirect conditions");
    const std::optional<core::IndirectAddressEvaluation> decoded =
        core::evaluate_indirect_address("7", "B6", core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target == 4,
            "grouped shifted selector should decode to final address 4");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("base"),      MachineItem::op(0x50, "С/П"),
        MachineItem::op(0x8b, "К БП b"), digit(),
        MachineItem::label("duplicate"), MachineItem::op(0x50, "С/П"),
        MachineItem::op(0x8b, "К БП b"), digit(),
    };

    const core::PostLayoutIndirectFlowResult result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B2", .counts_against_program = false}});

    require(result.applied == 1,
            "post-layout stop-tail reuse should reuse an existing preloaded stop tail");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "post-layout stop-tail reuse should remove one continuation cell");
    const auto duplicate_it =
        std::find_if(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Label && item.name == "duplicate";
        });
    require(duplicate_it != result.items.end(), "stop-tail test should keep duplicate label");
    const std::size_t duplicate_index =
        static_cast<std::size_t>(std::distance(result.items.begin(), duplicate_it));
    require(result.items.at(duplicate_index + 1).kind == MachineItemKind::Op &&
                result.items.at(duplicate_index + 1).opcode == 0x88,
            "duplicate stop tail should become K BP 8");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "post-layout-stop-tail-reuse",
            "stop-tail reuse should report the TS optimization name");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("base"),
        MachineItem::op(0x50, "С/П"),
        MachineItem::op(0x8b, "К БП b"),
    };
    for (int index = 0; index < 8; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("zero_tail"));
    program.push_back(digit());
    program.push_back(MachineItem::op(0x50, "С/П"));
    program.push_back(MachineItem::op(0x8b, "К БП b"));
    program.push_back(MachineItem::label("late_target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B2", .counts_against_program = false},
         PreloadReport{.register_name = "7", .value = "13", .counts_against_program = false}});

    require(result.applied == 1,
            "post-layout stop-tail reuse should handle zero-prefixed stop tails");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "zero-prefixed stop-tail reuse should remove one continuation cell");
    const auto zero_it =
        std::find_if(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Label && item.name == "zero_tail";
        });
    require(zero_it != result.items.end(), "zero-tail test should keep zero_tail label");
    const std::size_t zero_index =
        static_cast<std::size_t>(std::distance(result.items.begin(), zero_it));
    require(result.items.at(zero_index + 1).kind == MachineItemKind::Op &&
                result.items.at(zero_index + 1).opcode == 0x00,
            "zero-prefixed stop-tail reuse should keep the leading zero");
    require(result.items.at(zero_index + 2).kind == MachineItemKind::Op &&
                result.items.at(zero_index + 2).opcode == 0x88,
            "zero-prefixed stop-tail reuse should replace the stop with K BP 8");
    require(std::any_of(result.preloads.begin(), result.preloads.end(),
                        [](const PreloadReport& preload) {
                          return preload.register_name == "7" && preload.value == "C4" &&
                                 !preload.counts_against_program;
                        }),
            "zero-prefixed stop-tail reuse should retarget shifted selector R7 to C4");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("base"),      MachineItem::op(0x50, "С/П"),
        MachineItem::op(0x8b, "К БП b"), MachineItem::op(0x57, "F x!=0"),
        MachineItem::address("shim"),    digit(),
        MachineItem::label("shim"),      MachineItem::op(0x88, "К БП 8"),
    };

    const core::PostLayoutIndirectFlowResult result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B2", .counts_against_program = false}});

    require(result.applied == 1,
            "post-layout stop-tail reuse should rewrite branches to selector shims");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "branch-to-selector stop-tail reuse should remove the direct address cell");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Address &&
                                  std::get<std::string>(item.target) == "shim";
                         }),
            "branch-to-selector stop-tail reuse should remove the shim address target");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) {
                          return item.kind == MachineItemKind::Op && item.opcode == 0x78;
                        }),
            "branch-to-selector stop-tail reuse should emit K x!=0 8");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("main"),
        digit(),
    };
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B6", .counts_against_program = false}});

    require(result.applied == 1, "post-layout stop-tail reuse should reuse existing selector flow");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "existing selector flow should remove the direct address cell");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Address &&
                                  std::get<std::string>(item.target) == "target";
                         }),
            "existing selector flow should remove the direct target address");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) {
                          return item.kind == MachineItemKind::Op && item.opcode == 0x88;
                        }),
            "existing selector flow should emit K BP 8");
    require(result.preloads.size() == 1 && result.preloads.at(0).register_name == "8" &&
                result.preloads.at(0).value == "B5",
            "existing selector flow should retarget R8 to B5");
    const std::optional<core::IndirectAddressEvaluation> decoded =
        core::evaluate_indirect_address("8", "B5", core::IndirectOperationKind::Flow);
    require(decoded.has_value() && decoded->actual_flow_target == 3,
            "retargeted existing selector should decode to final target address 3");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const auto& optimization) {
                          return optimization.name == "post-layout-existing-selector-flow";
                        }),
            "existing selector flow should report the TS optimization name");
  }

  {
    MachineItem proc = MachineItem::label("finish_turn");
    proc.procedure_boundary = "start";
    std::vector<MachineItem> program = {
        MachineItem::label("main"),
        digit(),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("finish_turn"),
        MachineItem::op(0x88, "К БП 8"),
        proc,
        digit(),
        MachineItem::op(0x52, "В/О"),
    };
    program.at(2).comment = "proc call finish_turn";

    const core::PostLayoutIndirectFlowResult result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B2", .counts_against_program = false}});

    require(result.applied == 1,
            "post-layout stop-tail reuse should apply empty-stack tail-call rewrite");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 3,
            "adjacent empty-stack tail-call rewrite should remove the call, operand, and proved "
            "loop-back cell");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "post-layout-empty-stack-tail-call",
            "empty-stack post-layout rewrite should report the TS optimization name");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op &&
                                  (item.opcode == 0x51 || item.opcode == 0x53);
                         }),
            "adjacent empty-stack tail call should become natural fallthrough");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const auto& optimization) {
                          return optimization.name == "post-layout-empty-stack-tail-fallthrough";
                        }),
            "natural component entry should carry its own proof report");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0x88;
                         }),
            "empty-stack post-layout rewrite should remove the proved loop-back jump");
  }

  {
    MachineItem proc = MachineItem::label("finish_turn");
    proc.procedure_boundary = "start";
    std::vector<MachineItem> program = {
        MachineItem::label("main"),
        digit(),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("finish_turn"),
        MachineItem::label("unreferenced_loop_back_cell"),
        MachineItem::op(0x88, "К БП 8"),
        proc,
        digit(),
        MachineItem::op(0x52, "В/О"),
    };
    program.at(2).comment = "proc call finish_turn";
    const auto result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B2", .counts_against_program = false}});
    require(result.applied == 1 &&
                core::machine_cell_count(result.items) == core::machine_cell_count(program) - 3,
            "an unreferenced metadata label must not look like an external removed-cell entry");
  }

  {
    MachineItem proc = MachineItem::label("finish_turn");
    proc.procedure_boundary = "start";
    std::vector<MachineItem> program = {
        MachineItem::label("main"),
        MachineItem::op(0x5e, "F x=0"),
        MachineItem::address("loop_back_entry"),
        digit(),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("finish_turn"),
        MachineItem::label("loop_back_entry"),
        MachineItem::op(0x88, "К БП 8"),
        proc,
        digit(),
        MachineItem::op(0x52, "В/О"),
    };
    program.at(4).comment = "proc call finish_turn";
    const auto result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B2", .counts_against_program = false}});
    require(result.applied == 0 && result.items.size() == program.size(),
            "a direct external entry into the removed loop-back cell must reject fallthrough");
  }

  {
    MachineItem proc = MachineItem::label("finish_turn");
    proc.procedure_boundary = "start";
    std::vector<MachineItem> program = {
        MachineItem::label("main"),
        digit(),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("finish_turn"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address("main"),
        proc,
        digit(),
        MachineItem::op(0x52, "В/О"),
    };
    program.at(2).comment = "proc call finish_turn";

    const core::PostLayoutIndirectFlowResult result = core::optimize_post_layout_stop_tail_reuse(
        program,
        {PreloadReport{.register_name = "8", .value = "B8", .counts_against_program = false}});

    require(result.applied == 1,
            "empty-stack tail-call rewrite should recognize a direct jump to address zero; got " +
                std::to_string(result.applied));
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 4,
            "adjacent direct empty-stack tail-call rewrite should remove both transfers");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op &&
                                  (item.opcode == 0x51 || item.opcode == 0x53);
                         }),
            "adjacent direct tail call should fall through into its procedure");
    require(result.preloads.size() == 1 && result.preloads.at(0).register_name == "8" &&
                result.preloads.at(0).value == "B4",
            "direct empty-stack tail-call fallthrough should retarget selectors across four "
            "deletions");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::op(0x00, "0"),
        MachineItem::op(0x0a, "."),
        MachineItem::op(0x05, "5"),
        MachineItem::op(0x40, "X->П 0"),
    };
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    for (int index = 0; index < 94; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_fractional_r0_flow(program);

    require(result.applied == 1,
            "fractional R0 flow should rewrite when the final target is address 99");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "fractional R0 flow should shrink by one cell");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) {
                          return item.kind == MachineItemKind::Op && item.opcode == 0x80;
                        }),
            "fractional R0 jump should emit K BP 0");
    require(
        std::none_of(result.items.begin(), result.items.end(),
                     [](const MachineItem& item) { return item.kind == MachineItemKind::Address; }),
        "fractional R0 jump should remove the address cell");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::op(0x00, "0"),
        MachineItem::op(0x0a, "."),
        MachineItem::op(0x05, "5"),
        MachineItem::op(0x40, "X->П 0"),
    };
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    for (int index = 0; index < 93; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_fractional_r0_flow(program);

    require(result.applied == 0,
            "fractional R0 flow should keep code when final target is not address 99");
    require(result.items.size() == program.size(),
            "fractional R0 no-op should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "fractional R0 no-op should be byte-identical");
    }
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::op(0x00, "0"),        MachineItem::op(0x0a, "."),
        MachineItem::op(0x05, "5"),        MachineItem::op(0x40, "X->П 0"),
        MachineItem::op(0xd7, "К П->X 7"),
    };
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    for (int index = 0; index < 93; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_fractional_r0_flow(program);

    require(result.applied == 1,
            "fractional R0 flow should survive unrelated indirect memory access");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) {
                          return item.kind == MachineItemKind::Op && item.opcode == 0x80;
                        }),
            "fractional R0 flow through unrelated memory should emit K BP 0");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::op(0x00, "0"),
        MachineItem::op(0x0a, "."),
        MachineItem::op(0x05, "5"),
        MachineItem::op(0x40, "X->П 0"),
    };
    const std::vector<MachineItem> branch = call("target");
    program.insert(program.end(), branch.begin(), branch.end());
    for (int index = 0; index < 94; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x52, "В/О"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_fractional_r0_flow(program);

    require(result.applied == 1,
            "fractional R0 flow should rewrite calls whose target lands at address 99");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) {
                          return item.kind == MachineItemKind::Op && item.opcode == 0xa0;
                        }),
            "fractional R0 call should emit K PP 0");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1, "address/code overlay should apply to a labeled single-cell op");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "address/code overlay should remove the overlaid executable cell");
    require(result.items.at(0).kind == MachineItemKind::Op && result.items.at(0).opcode == 0x51,
            "address/code overlay should keep the direct jump op");
    require(result.items.at(1).kind == MachineItemKind::Label && result.items.at(1).name == "entry",
            "address/code overlay should move the entry label onto the address byte");
    require(result.items.at(2).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(2).target) == "target",
            "address/code overlay should keep the original branch target");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0x07;
                         }),
            "address/code overlay should remove the original executable op");
    require(!result.optimizations.empty() &&
                result.optimizations.at(0).name == "address-code-overlay",
            "address/code overlay should report the TS optimization name");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x01, "1"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should apply when the target label lands on its address byte");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "self-target address/code overlay should remove the overlaid executable cell");
    require(result.items.at(0).kind == MachineItemKind::Op && result.items.at(0).opcode == 0x51,
            "self-target address/code overlay should keep the direct jump op");
    require(result.items.at(1).kind == MachineItemKind::Label &&
                result.items.at(1).name == "target",
            "self-target address/code overlay should move the target label onto the address byte");
    require(result.items.at(2).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(2).target) == "target",
            "self-target address/code overlay should keep the original branch target");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0x01;
                         }),
            "self-target address/code overlay should remove the original executable op");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x51, "БП"));
    program.push_back(MachineItem::address("done"));
    for (int index = 0; index < 48; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    program.push_back(MachineItem::label("done"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should overlay address-taking ops when their operand remains");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "address-taking overlay should remove only the opcode cell");
    require(result.items.at(0).kind == MachineItemKind::Op && result.items.at(0).opcode == 0x51,
            "address-taking overlay should keep the outer direct jump op");
    require(result.items.at(1).kind == MachineItemKind::Label && result.items.at(1).name == "entry",
            "address-taking overlay should move entry onto the outer address byte");
    require(result.items.at(2).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(2).target) == "target",
            "address-taking overlay should keep the outer branch target");
    require(result.items.at(3).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(3).target) == "done",
            "address-taking overlay should keep the overlaid op operand");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::address(7));
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should overlay orphan address bytes as executable cells");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "orphan address overlay should remove the original address byte");
    require(result.items.at(1).kind == MachineItemKind::Label && result.items.at(1).name == "entry",
            "orphan address overlay should move entry onto the branch address byte");
    require(result.items.at(2).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(2).target) == "target",
            "orphan address overlay should keep the outer branch target");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    MachineItem formal_byte = MachineItem::address(55);
    formal_byte.formal_opcode = 0x55;
    formal_byte.comment = "formal K1 byte";
    program.push_back(formal_byte);
    for (int index = 0; index < 53; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should overlay formal address bytes as executable cells");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "formal address-byte overlay should remove the original formal address cell");
    require(result.items.at(1).kind == MachineItemKind::Label && result.items.at(1).name == "entry",
            "formal address-byte overlay should move entry onto the branch address byte");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Address &&
                                  item.formal_opcode.has_value() && *item.formal_opcode == 0x55;
                         }),
            "formal address-byte overlay should remove the original formal opcode");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = jump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0xb5, "К X->П 5"));
    program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should use a formal alias for the outer address byte");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "formal alias overlay should remove the original executable cell");
    require(result.items.at(2).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(2).target) == "target" &&
                result.items.at(2).formal_opcode.has_value() &&
                *result.items.at(2).formal_opcode == 0xb5,
            "formal alias overlay should preserve the label target through formalOpcode B5");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0xb5;
                         }),
            "formal alias overlay should remove the original executable op");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("top"),
        MachineItem::op(0x51, "БП"),
    };
    MachineItem formal_branch = MachineItem::address(105);
    formal_branch.formal_opcode = 0xa5;
    formal_branch.comment = "short-side formal address to 00";
    program.push_back(formal_branch);
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0xa5, "К ПП 5"));
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should allow formal branch addresses whose actual target is "
            "before the removed cell");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "formal branch address overlay should remove the original executable cell");
    require(result.items.at(1).kind == MachineItemKind::Op && result.items.at(1).opcode == 0x51,
            "formal branch address overlay should keep the direct jump op");
    require(result.items.at(2).kind == MachineItemKind::Label && result.items.at(2).name == "entry",
            "formal branch address overlay should move entry onto the formal address byte");
    require(result.items.at(3).kind == MachineItemKind::Address &&
                std::get<int>(result.items.at(3).target) == 105 &&
                result.items.at(3).formal_opcode.has_value() &&
                *result.items.at(3).formal_opcode == 0xa5,
            "formal branch address overlay should preserve the branch formal opcode");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::op(0x51, "БП"),
        MachineItem::address(7),
        MachineItem::label("entry"),
        MachineItem::op(0x07, "7"),
    };
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::op(0x50, "С/П"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should refuse fixed numeric branch targets that would shift");
    require(result.items.size() == program.size(),
            "fixed numeric rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "fixed numeric rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = call("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    const std::vector<MachineItem> terminal_loop = jump("target");
    program.insert(program.end(), terminal_loop.begin(), terminal_loop.end());

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should apply to call continuations with terminal targets");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "terminal-call overlay should remove the overlaid executable cell");
    require(result.items.at(0).kind == MachineItemKind::Op && result.items.at(0).opcode == 0x53,
            "terminal-call overlay should keep the direct call op");
    require(result.items.at(1).kind == MachineItemKind::Label && result.items.at(1).name == "entry",
            "terminal-call overlay should move the continuation label onto the call address byte");
    require(result.items.at(2).kind == MachineItemKind::Address &&
                std::get<std::string>(result.items.at(2).target) == "target",
            "terminal-call overlay should keep the original call target");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0x07;
                         }),
            "terminal-call overlay should remove the original continuation op");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = call("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x52, "В/О"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should reject call continuations whose target returns");
    require(result.items.size() == program.size(),
            "returning-call rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "returning-call rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> outer_jump = jump("target");
    program.insert(program.end(), outer_jump.begin(), outer_jump.end());
    const std::vector<MachineItem> skip_entry = jump("after_entry");
    program.insert(program.end(), skip_entry.begin(), skip_entry.end());
    program.push_back(MachineItem::label("entry"));
    MachineItem indirect_jump = MachineItem::op(0x8e, "К БП e");
    indirect_jump.comment = "proved distant overlay indirect-target=0";
    program.push_back(indirect_jump);
    program.push_back(MachineItem::label("after_entry"));
    for (int index = 0; index < 90; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    const std::vector<MachineItem> entry_jump = jump("entry");
    program.insert(program.end(), entry_jump.begin(), entry_jump.end());

    std::vector<MachineItem> unproved_program = program;
    const auto unproved_jump =
        std::find_if(unproved_program.begin(), unproved_program.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x8e;
        });
    require(unproved_jump != unproved_program.end(),
            "indirect-jump overlay fixture should contain its candidate opcode");
    unproved_jump->comment.reset();
    const core::PostLayoutIndirectFlowResult unproved =
        core::optimize_post_layout_address_code_overlay(unproved_program);
    require(unproved.applied == 0,
            "distant indirect-jump overlay should require a final target artifact");

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should move a distant unconditional indirect jump");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "indirect-jump overlay should remove the original executable cell");
    const auto entry =
        std::find_if(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Label && item.name == "entry";
        });
    require(entry != result.items.end() && std::next(entry) != result.items.end() &&
                std::next(entry)->kind == MachineItemKind::Address &&
                std::next(entry)->formal_opcode.has_value() &&
                *std::next(entry)->formal_opcode == 0x8e,
            "indirect-jump overlay should execute the formal address byte at the moved entry");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0x8e;
                         }),
            "indirect-jump overlay should remove the original indirect jump opcode");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> outer_jump = jump("target");
    program.insert(program.end(), outer_jump.begin(), outer_jump.end());
    const std::vector<MachineItem> relocated_continuation = jump("done");
    program.insert(program.end(), relocated_continuation.begin(), relocated_continuation.end());
    for (int index = 0; index < 3; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    const std::vector<MachineItem> skip_entry = jump("after_entry");
    program.insert(program.end(), skip_entry.begin(), skip_entry.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    const std::vector<MachineItem> original_continuation = jump("done");
    program.insert(program.end(), original_continuation.begin(), original_continuation.end());
    program.push_back(MachineItem::label("after_entry"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    program.push_back(MachineItem::label("done"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    const std::vector<MachineItem> entry_jump = jump("entry");
    program.insert(program.end(), entry_jump.begin(), entry_jump.end());

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 1,
            "address/code overlay should move a distant op across equivalent jump tails");
    require(core::machine_cell_count(result.items) == core::machine_cell_count(program) - 1,
            "equivalent-continuation overlay should remove the original executable cell");
    const auto entry =
        std::find_if(result.items.begin(), result.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Label && item.name == "entry";
        });
    require(entry != result.items.end() && std::next(entry) != result.items.end() &&
                std::next(entry)->kind == MachineItemKind::Address,
            "equivalent-continuation overlay should move the entry label onto the address byte");
    require(std::none_of(result.items.begin(), result.items.end(),
                         [](const MachineItem& item) {
                           return item.kind == MachineItemKind::Op && item.opcode == 0x07;
                         }),
            "equivalent-continuation overlay should remove the original opcode");

    std::vector<MachineItem> mismatched = program;
    const auto old_tail =
        std::find_if(mismatched.begin(), mismatched.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Label && item.name == "entry";
        });
    require(old_tail != mismatched.end(),
            "equivalent-continuation fixture should contain its old entry");
    const auto old_tail_address =
        std::find_if(std::next(old_tail, 2), mismatched.end(),
                     [](const MachineItem& item) { return item.kind == MachineItemKind::Address; });
    require(old_tail_address != mismatched.end(),
            "equivalent-continuation fixture should contain its old jump operand");
    old_tail_address->target = std::string("after_entry");
    const core::PostLayoutIndirectFlowResult rejected =
        core::optimize_post_layout_address_code_overlay(mismatched);
    require(rejected.applied == 0,
            "address/code overlay should reject different old and new jump tails");
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> branch = cjump("target");
    program.insert(program.end(), branch.begin(), branch.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x52, "В/О"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should reject conditional branch continuations");
    require(result.items.size() == program.size(),
            "conditional rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "conditional rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> sub_call = call("sub");
    program.insert(program.end(), sub_call.begin(), sub_call.end());
    program.push_back(digit());
    const std::vector<MachineItem> after_jump = jump("after");
    program.insert(program.end(), after_jump.begin(), after_jump.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    program.push_back(digit());
    program.push_back(digit());
    program.push_back(MachineItem::label("sub"));
    program.push_back(MachineItem::op(0x52, "В/О"));
    program.push_back(MachineItem::label("after"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    const std::vector<MachineItem> entry_jump = jump("entry");
    program.insert(program.end(), entry_jump.begin(), entry_jump.end());

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should keep separate entries off returning-call flows");
    require(result.items.size() == program.size(),
            "returning-call separate-entry rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "returning-call separate-entry rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> target_jump = jump("target");
    program.insert(program.end(), target_jump.begin(), target_jump.end());
    program.push_back(MachineItem::op(0x02, "2"));
    const std::vector<MachineItem> after_jump = jump("after");
    program.insert(program.end(), after_jump.begin(), after_jump.end());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x00, "0"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    program.push_back(MachineItem::label("after"));
    program.push_back(MachineItem::op(0x50, "С/П"));
    const std::vector<MachineItem> entry_jump = jump("entry");
    program.insert(program.end(), entry_jump.begin(), entry_jump.end());

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should not move digits into numeric-entry continuations");
    require(result.items.size() == program.size(),
            "numeric-entry rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "numeric-entry rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> sub_call = call("sub");
    program.insert(program.end(), sub_call.begin(), sub_call.end());
    program.push_back(digit());
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    program.push_back(digit());
    program.push_back(digit());
    program.push_back(digit());
    program.push_back(MachineItem::label("sub"));
    program.push_back(MachineItem::op(0x52, "В/О"));
    const std::vector<MachineItem> entry_jump = jump("entry");
    program.insert(program.end(), entry_jump.begin(), entry_jump.end());

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should keep labeled entries reached by ordinary fallthrough");
    require(result.items.size() == program.size(),
            "ordinary-fallthrough rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "ordinary-fallthrough rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program;
    const std::vector<MachineItem> entry_call = call("entry");
    program.insert(program.end(), entry_call.begin(), entry_call.end());
    program.push_back(digit());
    program.push_back(MachineItem::op(0x50, "С/П"));
    program.push_back(MachineItem::label("entry"));
    program.push_back(MachineItem::op(0x07, "7"));
    program.push_back(digit());
    program.push_back(MachineItem::op(0x52, "В/О"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should keep returning call targets off their own address byte");
    require(result.items.size() == program.size(),
            "returning target rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "returning target rejection should preserve items exactly");
    }
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::op(0x57, "F x!=0"),
        MachineItem::address("target"),
        MachineItem::label("entry"),
        MachineItem::op(0x07, "7"),
    };
    for (int index = 0; index < 5; ++index)
      program.push_back(digit());
    program.push_back(MachineItem::label("target"));
    program.push_back(MachineItem::op(0x52, "В/О"));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_address_code_overlay(program);

    require(result.applied == 0,
            "address/code overlay should reject conditional x!=0 continuations");
    require(result.items.size() == program.size(),
            "x!=0 conditional rejection should preserve item count");
    for (std::size_t index = 0; index < program.size(); ++index) {
      require(machine_items_equal(result.items.at(index), program.at(index)),
              "x!=0 conditional rejection should preserve items exactly");
    }
  }

  // Runtime-charged selector reuse: a direct backward call to an address that
  // a stable register provably holds on every path becomes a one-cell
  // indirect call.
  {
    const auto charged_program = [](bool clobber) {
      std::vector<MachineItem> program = {
          MachineItem::op(0x51, "БП"),
          MachineItem::address("charged_start"),
          MachineItem::label("charged_helper"),
          MachineItem::op(0x22, "F x²"),
          MachineItem::op(0x52, "В/О"),
          MachineItem::label("charged_start"),
          MachineItem::op(0x00, "0"),
          MachineItem::op(0x02, "2"),
          MachineItem::op(0x48, "X→П 8"),
      };
      if (clobber) {
        program.push_back(MachineItem::op(0x10, "+"));
        program.push_back(MachineItem::op(0x48, "X→П 8"));
      }
      program.push_back(MachineItem::op(0x53, "ПП"));
      program.push_back(MachineItem::address("charged_helper"));
      MachineItem stop = MachineItem::op(0x50, "С/П");
      stop.stop_disposition = StopDisposition::Terminal;
      program.push_back(std::move(stop));
      return program;
    };

    const std::vector<MachineItem> program = charged_program(false);
    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_stop_tail_reuse(program, {});
    require(result.applied == 1 &&
                core::machine_cell_count(result.items) ==
                    core::machine_cell_count(program) - 1,
            "a runtime-charged stable register should shorten a backward direct call");
    require(std::any_of(result.items.begin(), result.items.end(),
                        [](const MachineItem& item) {
                          return item.kind == MachineItemKind::Op && item.opcode == 0xa8;
                        }),
            "the charged rewrite should emit К ПП 8");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "post-layout-charged-selector-flow";
                        }),
            "the charged rewrite should report its own optimization name");

    // The machine may re-deliver the selector in its raw zero-padded BCD form
    // (2 -> 00000002); that spelling is numerically identical and cannot be
    // distinguished by any machine operation, so states are compared after
    // numeric canonicalization.
    const auto canonical_number = [](std::string text) {
      std::erase(text, ' ');
      std::replace(text.begin(), text.end(), ',', '.');
      if (!text.empty() && text.back() == '.')
        text.pop_back();
      const std::size_t sign = !text.empty() && text.front() == '-' ? 1U : 0U;
      std::size_t nonzero = sign;
      while (nonzero + 1U < text.size() && text.at(nonzero) == '0' &&
             std::isdigit(static_cast<unsigned char>(text.at(nonzero + 1U))) != 0) {
        ++nonzero;
      }
      return text.substr(0, sign) + text.substr(nonzero);
    };
    std::vector<std::pair<std::string, std::string>> observed;
    for (const std::vector<MachineItem>* variant : {&program, &result.items}) {
      emulator::MK61 calc;
      calc.load_program(resolved_opcodes(*variant));
      calc.press("В/О");
      calc.press("С/П");
      const emulator::RunResult run = calc.run_until_stable(300, 5);
      require(run.stopped, "both charged-selector layouts should stop");
      observed.emplace_back(canonical_number(calc.read_register("x")),
                            canonical_number(calc.read_register("8")));
    }
    require(observed.at(0) == observed.at(1),
            "the charged-selector rewrite should preserve the numeric X and R8 state");

    const std::vector<MachineItem> clobbered = charged_program(true);
    const core::PostLayoutIndirectFlowResult rejected =
        core::optimize_post_layout_stop_tail_reuse(clobbered, {});
    require(rejected.applied == 0,
            "an unknown overwrite between the charge and the call must reject the rewrite");
  }

  // A join of two different charged values must not prove either one, and a
  // forward flow (target after the operand cell) stays direct even when the
  // value is known.
  {
    std::vector<MachineItem> ambiguous = {
        MachineItem::op(0x51, "БП"),
        MachineItem::address("ambiguous_start"),
        MachineItem::label("ambiguous_helper"),
        MachineItem::op(0x22, "F x²"),
        MachineItem::op(0x52, "В/О"),
        MachineItem::label("ambiguous_start"),
        MachineItem::op(0x5e, "F x=0"),
        MachineItem::address("ambiguous_other"),
        MachineItem::op(0x00, "0"),
        MachineItem::op(0x02, "2"),
        MachineItem::op(0x48, "X→П 8"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address("ambiguous_join"),
        MachineItem::label("ambiguous_other"),
        MachineItem::op(0x00, "0"),
        MachineItem::op(0x03, "3"),
        MachineItem::op(0x48, "X→П 8"),
        MachineItem::label("ambiguous_join"),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("ambiguous_helper"),
    };
    MachineItem stop = MachineItem::op(0x50, "С/П");
    stop.stop_disposition = StopDisposition::Terminal;
    ambiguous.push_back(std::move(stop));

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_stop_tail_reuse(ambiguous, {});
    require(std::none_of(result.optimizations.begin(), result.optimizations.end(),
                         [](const core::passes::AppliedOptimization& optimization) {
                           return optimization.name == "post-layout-charged-selector-flow";
                         }),
            "joining two different charged values must not prove a selector");

    std::vector<MachineItem> forward = {
        MachineItem::op(0x00, "0"),
        MachineItem::op(0x05, "5"),
        MachineItem::op(0x48, "X→П 8"),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("forward_helper"),
        MachineItem::label("forward_helper"),
        MachineItem::op(0x22, "F x²"),
        MachineItem::op(0x52, "В/О"),
    };
    MachineItem forward_stop = MachineItem::op(0x50, "С/П");
    forward_stop.stop_disposition = StopDisposition::Terminal;
    forward.push_back(std::move(forward_stop));

    const core::PostLayoutIndirectFlowResult forward_result =
        core::optimize_post_layout_stop_tail_reuse(forward, {});
    require(std::none_of(forward_result.optimizations.begin(),
                         forward_result.optimizations.end(),
                         [](const core::passes::AppliedOptimization& optimization) {
                           return optimization.name == "post-layout-charged-selector-flow";
                         }),
            "a forward direct flow must stay direct even when the charged value is known");
  }

  // Empty-stack loop return: a direct БП to physical 01 with a provably empty
  // return stack becomes a one-cell В/О (the machine continues at 01 after an
  // empty-stack В/О; pinned by emulator_vo_empty_continuation_facts).
  {
    const std::vector<MachineItem> loop_program = [] {
      std::vector<MachineItem> program = {
          MachineItem::op(0x54, "К НОП"),
          MachineItem::label("loop_head"),
          MachineItem::op(0x60, "П→X 0"),
          [] {
            MachineItem stop = MachineItem::op(0x50, "С/П");
            stop.stop_disposition = StopDisposition::Resumable;
            return stop;
          }(),
          MachineItem::op(0x40, "X→П 0"),
          MachineItem::op(0x5e, "F x=0"),
          MachineItem::address("loop_other"),
          MachineItem::op(0x53, "ПП"),
          MachineItem::address("loop_helper"),
          MachineItem::op(0x51, "БП"),
          MachineItem::address("loop_head"),
          MachineItem::label("loop_other"),
          MachineItem::op(0x01, "1"),
          MachineItem::op(0x41, "X→П 1"),
          MachineItem::op(0x51, "БП"),
          MachineItem::address("loop_head"),
          MachineItem::label("loop_helper"),
          MachineItem::op(0x22, "F x²"),
          MachineItem::op(0x52, "В/О"),
      };
      return program;
    }();

    const core::PostLayoutIndirectFlowResult result =
        core::optimize_post_layout_empty_stack_loop_return(loop_program, {});
    require(result.applied == 2,
            "both main-level jumps to physical 01 should become one-cell В/О");
    require(core::machine_cell_count(result.items) ==
                core::machine_cell_count(loop_program) - 2,
            "each converted loop return should save exactly one cell");
    int converted = 0;
    for (const MachineItem& item : result.items) {
      if (item.kind == MachineItemKind::Op && item.opcode == 0x52 &&
          item.comment == "optimized БП 01") {
        ++converted;
      }
    }
    require(converted == 2, "converted loop returns should carry the shared В/О marker");
    require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                        [](const core::passes::AppliedOptimization& optimization) {
                          return optimization.name == "post-layout-empty-stack-loop-return";
                        }),
            "the loop-return rewrite should report its own optimization name");

    // Behavioral equivalence over one full loop iteration on the emulator.
    std::vector<std::string> observed;
    for (const std::vector<MachineItem>* variant : {&loop_program, &result.items}) {
      emulator::MK61 calc;
      calc.load_program(resolved_opcodes(*variant));
      calc.press("В/О");
      calc.press("С/П");
      const emulator::RunResult first = calc.run_until_stable(300, 5);
      require(first.stopped, "both loop-return layouts should reach the first stop");
      calc.press("С/П");
      const emulator::RunResult second = calc.run_until_stable(300, 5);
      require(second.stopped, "both loop-return layouts should loop back to the stop");
      observed.push_back(calc.read_register("x") + "|" + calc.read_register("0"));
    }
    require(observed.at(0) == observed.at(1),
            "the loop-return rewrite should preserve the observable loop behavior");
  }

  // The loop-return rewrite must fail closed for every unproved shape: a jump
  // to a head that is not physical 01, a jump reachable with a non-empty
  // return stack, and a deletion that would break a numeric operand encoding.
  {
    // Head at physical 02: the empty-stack В/О continuation is pinned to 01.
    std::vector<MachineItem> wrong_head = {
        MachineItem::op(0x54, "К НОП"),
        MachineItem::op(0x54, "К НОП"),
        MachineItem::label("wrong_head"),
        MachineItem::op(0x60, "П→X 0"),
        [] {
          MachineItem stop = MachineItem::op(0x50, "С/П");
          stop.stop_disposition = StopDisposition::Resumable;
          return stop;
        }(),
        MachineItem::op(0x40, "X→П 0"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address("wrong_head"),
    };
    const core::PostLayoutIndirectFlowResult wrong_head_result =
        core::optimize_post_layout_empty_stack_loop_return(wrong_head, {});
    require(wrong_head_result.applied == 0,
            "a loop head away from physical 01 must not be converted");

    // The jump to 01 sits inside a called helper: its execution state carries
    // a return frame, and В/О would pop that frame instead of looping.
    std::vector<MachineItem> framed = {
        MachineItem::op(0x54, "К НОП"),
        MachineItem::label("framed_head"),
        MachineItem::op(0x60, "П→X 0"),
        [] {
          MachineItem stop = MachineItem::op(0x50, "С/П");
          stop.stop_disposition = StopDisposition::Resumable;
          return stop;
        }(),
        MachineItem::op(0x53, "ПП"),
        MachineItem::address("framed_helper"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address("framed_head"),
        MachineItem::label("framed_helper"),
        MachineItem::op(0x22, "F x²"),
        MachineItem::op(0x5e, "F x=0"),
        MachineItem::address("framed_exit"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address("framed_head"),
        MachineItem::label("framed_exit"),
        MachineItem::op(0x52, "В/О"),
    };
    const core::PostLayoutIndirectFlowResult framed_result =
        core::optimize_post_layout_empty_stack_loop_return(framed, {});
    for (const core::passes::AppliedOptimization& optimization :
         framed_result.optimizations) {
      require(optimization.name != "post-layout-empty-stack-loop-return" ||
                  framed_result.applied <= 1,
              "only the empty-stack jump may be converted in the framed program");
    }
    // The main-level jump converts; the in-helper jump must survive as БП.
    int framed_direct_jumps = 0;
    for (const MachineItem& item : framed_result.items) {
      if (item.kind == MachineItemKind::Op && item.opcode == 0x51)
        ++framed_direct_jumps;
    }
    require(framed_direct_jumps >= 1,
            "the helper's jump to 01 must stay direct because its return stack is not empty");

    // A numeric operand beyond the deleted cell pins every later address.
    std::vector<MachineItem> pinned = {
        MachineItem::op(0x54, "К НОП"),
        MachineItem::label("pinned_head"),
        MachineItem::op(0x60, "П→X 0"),
        [] {
          MachineItem stop = MachineItem::op(0x50, "С/П");
          stop.stop_disposition = StopDisposition::Resumable;
          return stop;
        }(),
        MachineItem::op(0x40, "X→П 0"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address("pinned_head"),
        MachineItem::op(0x5e, "F x=0"),
        MachineItem::address(8),
        [] {
          MachineItem stop = MachineItem::op(0x50, "С/П");
          stop.stop_disposition = StopDisposition::Terminal;
          return stop;
        }(),
    };
    const core::PostLayoutIndirectFlowResult pinned_result =
        core::optimize_post_layout_empty_stack_loop_return(pinned, {});
    require(pinned_result.applied == 0,
            "a numeric operand beyond the deleted cell must reject the loop-return rewrite");
  }
}

} // namespace mkpro::tests
