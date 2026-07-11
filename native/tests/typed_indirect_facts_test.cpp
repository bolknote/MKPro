#include "mkpro/compiler.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/core/passes/preloaded_indirect_flow.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem terminal_stop() {
  MachineItem item = MachineItem::op(0x50, "С/П");
  item.stop_disposition = StopDisposition::Terminal;
  return item;
}

bool reason_contains(const core::AuthoritativePostLayoutControlFlow& result,
                     const std::string& fragment) {
  return std::any_of(result.reasons.begin(), result.reasons.end(),
                     [&](const std::string& reason) {
                       return reason.find(fragment) != std::string::npos;
                     });
}

} // namespace

void typed_indirect_facts_are_materialized_and_relocated() {
  {
    std::vector<MachineItem> incomplete = {
        MachineItem::op(0x87, "К БП 7"), MachineItem::label("opaque_target"), terminal_stop()};
    const auto rejected = core::build_post_layout_control_flow(incomplete);
    require(!rejected.proved && reason_contains(rejected, "indirect-flow fact is missing"),
            "an indirect command without a complete typed target set must fail closed");

    incomplete.at(0).indirect_flow_targets =
        std::vector<IrTarget>{std::string("opaque_target")};
    const auto accepted = core::build_post_layout_control_flow(incomplete);
    require(accepted.proved && accepted.indirect_flow_targets.size() == 1U,
            "the same command with an opaque typed target identity should prove");
  }

  {
    IrOp target_label;
    target_label.kind = IrKind::Label;
    target_label.name = "opaque_leaf";
    IrOp stop;
    stop.kind = IrKind::Stop;
    stop.opcode = 0x50;
    stop.meta.mnemonic = "С/П";
    stop.meta.stop_disposition = StopDisposition::Terminal;
    IrOp main_label;
    main_label.kind = IrKind::Label;
    main_label.name = "opaque_driver";
    IrOp direct;
    direct.kind = IrKind::Jump;
    direct.opcode = 0x51;
    direct.target = 0;
    direct.meta.mnemonic = "БП";
    CompileOptions options;
    options.budget = 999;
    const core::passes::PassContext context{.options = options};
    const core::passes::PassResult rewritten = core::passes::run_preloaded_indirect_flow(
        {target_label, stop, main_label, direct}, context,
        core::passes::IndirectFlowOptions{.relax_max_target_guard = true});
    require(rewritten.applied == 1,
            "pre-layout pass should convert an unrelated backward symbolic jump");
    const auto indirect = std::find_if(rewritten.ops.begin(), rewritten.ops.end(),
                                       [](const IrOp& op) {
                                         return op.kind == IrKind::IndirectJump;
                                       });
    require(indirect != rewritten.ops.end() &&
                indirect->meta.indirect_flow_targets ==
                    std::optional<std::vector<IrTarget>>{
                        std::vector<IrTarget>{0}},
            "preloaded indirect IR must carry its proved final numeric target");
    core::PostLayoutControlFlowOptions cfg_options;
    cfg_options.main_entry = std::string("opaque_driver");
    const auto control = core::build_post_layout_control_flow(
        lower_ir_to_machine(rewritten.ops), cfg_options);
    require(control.proved && control.indirect_flow_targets.size() == 1U,
            "preloaded IR target fact must survive lowering into authoritative CFG");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("opaque_main"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address(std::string("opaque_target")),
        MachineItem::op(0x0d, "Cx"),
        MachineItem::label("opaque_target"),
        terminal_stop(),
    };
    CompileOptions options;
    options.budget = 999;
    options.analysis = true;
    const core::PostLayoutIndirectFlowResult rewritten =
        core::optimize_post_layout_indirect_flow(program, options, 0);
    require(rewritten.applied == 1 && core::machine_cell_count(rewritten.items) == 3,
            "forward post-layout rewrite should delete one direct operand cell");
    const auto indirect = std::find_if(
        rewritten.items.begin(), rewritten.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && (item.opcode & 0xf0) == 0x80;
        });
    require(indirect != rewritten.items.end() &&
                indirect->indirect_flow_targets ==
                    std::optional<std::vector<IrTarget>>{
                        std::vector<IrTarget>{std::string("opaque_target")}},
            "post-layout flow should retain the deleted operand as an opaque identity");
    const auto control = core::build_post_layout_control_flow(rewritten.items);
    require(control.proved && control.indirect_flow_targets.begin()->second.front().address == 2,
            "typed target identity should re-resolve after the target shifts by one cell");
  }

  {
    std::vector<MachineItem> program = {
        MachineItem::label("opaque_driver"),
        MachineItem::op(0x51, "БП"),
        MachineItem::address(std::string("opaque_tail")),
        MachineItem::op(0x0d, "Cx"),
        MachineItem::label("opaque_tail"),
        terminal_stop(),
    };
    const core::PostLayoutIndirectFlowResult rewritten =
        core::optimize_post_layout_stop_tail_reuse(
            program,
            {PreloadReport{.register_name = "7",
                           .value = "B5",
                           .counts_against_program = false}});
    require(rewritten.applied == 1 && rewritten.preloads.size() == 1U &&
                rewritten.preloads.front().value == "B4",
            "machine BranchRewrite should retarget its selector after deleting the operand");
    const auto indirect = std::find_if(
        rewritten.items.begin(), rewritten.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x87;
        });
    require(indirect != rewritten.items.end() &&
                indirect->indirect_flow_targets ==
                    std::optional<std::vector<IrTarget>>{
                        std::vector<IrTarget>{std::string("opaque_tail")}},
            "machine BranchRewrite must preserve target identity independently of comments");
    const auto control = core::build_post_layout_control_flow(rewritten.items);
    require(control.proved && control.indirect_flow_targets.begin()->second.front().address == 2,
            "BranchRewrite target identity must rebind to the shifted executable cell");
  }

  {
    CompileOptions options;
    options.budget = 999;
    options.analysis = true;
    options.disable_candidate_search = true;
    options.disable_aggressive_post_layout = true;
    options.disable_interprocedural_opts = true;
    const CompileResult compiled = compile_source(R"mkpro(
program TypedIndexedBank {
  state {
    slots: packed[1..3] = 0
    index: counter 0..2 = 1
    scratch: packed = 0
  }
  loop {
    slots[index + 1] = 7
    scratch = 3
    halt(slots[index + 1])
  }
}
)mkpro",
                                                  options);
    require(compiled.implemented && compiled.diagnostics.empty(),
            "generic indexed-bank fact fixture should compile");
    const CompileResult recalled = compile_source(R"mkpro(
program TypedIndexedRecall {
  state {
    slots: packed[1..3] = [1, 2, 3]
    index: counter 0..2 = 1
  }
  loop {
    halt(slots[index + 1])
  }
}
)mkpro",
                                                  options);
    require(recalled.implemented && recalled.diagnostics.empty(),
            "generic indexed-bank recall fixture should compile");

    int indirect_memory_commands = 0;
    int indirect_stores = 0;
    int indirect_recalls = 0;
    const auto inspect = [&](const CompileResult& artifact) {
      for (const MachineItem& item : artifact.items) {
        if (item.kind != MachineItemKind::Op ||
            ((item.opcode & 0xf0) != 0xb0 && (item.opcode & 0xf0) != 0xd0)) {
          continue;
        }
        ++indirect_memory_commands;
        indirect_stores += (item.opcode & 0xf0) == 0xb0;
        indirect_recalls += (item.opcode & 0xf0) == 0xd0;
        require(item.indirect_memory_targets.has_value() &&
                    item.indirect_memory_targets->size() == 3U,
                "indexed lowering must carry its complete physical bank target vector");
      }
    };
    inspect(compiled);
    inspect(recalled);
    require(indirect_stores >= 1 && indirect_recalls >= 1,
            "indexed-bank fixture should exercise both indirect store and recall");
    const auto control = core::build_post_layout_control_flow(compiled.items);
    const auto recall_control = core::build_post_layout_control_flow(recalled.items);
    require(control.proved && recall_control.proved &&
                control.indirect_memory_targets.size() +
                        recall_control.indirect_memory_targets.size() ==
                    static_cast<std::size_t>(indirect_memory_commands),
            "authoritative CFG should retain every indexed-memory target set");
  }

  {
    std::vector<MachineItem> empty_return = {
        MachineItem::op(0x52, "В/О"), MachineItem::label("opaque_head"), terminal_stop()};
    const auto rejected = core::build_post_layout_control_flow(empty_return);
    require(!rejected.proved && reason_contains(rejected, "empty return stack"),
            "empty return must remain fail-closed without a typed machine-profile fact");

    core::PostLayoutControlFlowOptions options;
    options.empty_return_target = std::string("opaque_head");
    const auto accepted = core::build_post_layout_control_flow(empty_return, options);
    require(accepted.proved && accepted.empty_return_target.has_value() &&
                accepted.empty_return_target->address == 1,
            "explicit empty-return target should bind to one executable command identity");

    emulator::MK61 calc;
    calc.load_program({0x50, 0x01, 0x40, 0x52, 0x50});
    calc.press_sequence({"В/О", "С/П"});
    calc.run_until_stable(50, 3);
    calc.press_sequence({"С/П"});
    calc.run_until_stable(50, 3);
    require(calc.read_register("0").find('1') != std::string::npos,
            "emulator must pin empty В/О continuation through physical 01");
  }
}

} // namespace mkpro::tests

#ifdef MKPRO_TYPED_INDIRECT_FACTS_STANDALONE
int main() {
  try {
    mkpro::tests::typed_indirect_facts_are_materialized_and_relocated();
    std::cout << "typed_indirect_facts_test: ok\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "typed_indirect_facts_test: " << error.what() << '\n';
    return 1;
  }
}
#endif
