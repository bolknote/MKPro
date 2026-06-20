#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"

#include "test_support.hpp"

#include <algorithm>
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

} // namespace

void post_layout_indirect_flow_matches_typescript_contract() {
  CompileOptions options;
  options.delivery = DeliveryMode::Manual;
  options.budget = 999999;
  options.analysis = true;

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
}

} // namespace mkpro::tests
