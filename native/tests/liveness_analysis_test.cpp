#include "mkpro/core/passes/liveness_analysis.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp recall(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.register_name = std::move(register_name);
  op.opcode = 0x60;
  op.meta.mnemonic = "П->X";
  return op;
}

IrOp store(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = std::move(register_name);
  op.opcode = 0x40;
  op.meta.mnemonic = "X->П";
  return op;
}

IrOp indirect_store(std::string selector, std::optional<std::vector<int>> targets) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(selector);
  op.opcode = 0xb0;
  op.meta.mnemonic = "К X->П";
  op.meta.indirect_memory_targets = std::move(targets);
  return op;
}

IrOp indirect_recall(std::string selector, std::optional<std::vector<int>> targets) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(selector);
  op.opcode = 0xd0;
  op.meta.mnemonic = "К П->X";
  op.meta.indirect_memory_targets = std::move(targets);
  return op;
}

IrOp indirect_jump(std::string selector) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП";
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp jump_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  return op;
}

IrOp jump_to_address(int target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = target;
  op.meta.mnemonic = "БП";
  return op;
}

IrOp call_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = std::move(target);
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp halt() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  return op;
}

} // namespace

void liveness_analysis_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {label("loop"), recall("3"), plain(0x10, "+"),
                                       jump_to("loop")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("3"),
            "liveness did not propagate use backwards through loop body");
    require(info.live_out.at(3).contains("3"),
            "liveness did not propagate use through loop back edge");
  }

  {
    const std::vector<IrOp> program = {call_to("terminal"), recall("3"), label("terminal"), halt()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.live_out.at(0).contains("3"),
            "liveness propagated through non-returning direct call continuation");
  }

  {
    const std::vector<IrOp> program = {call_to("returns"), recall("3"), halt(), label("returns"),
                                       ret()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(0).contains("3"),
            "liveness did not propagate direct call continuation through return");
  }

  {
    const std::vector<IrOp> program = {
        call_to("outer"), recall("9"), halt(),         label("outer"),
        call_to("inner"), ret(),       label("inner"), ret()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(0).contains("9") && info.live_out.at(4).contains("9"),
            "liveness lost a caller continuation through nested calls and returns");
  }

  {
    const std::vector<IrOp> program = {store("1"), halt(), recall("1")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(1).contains("1"),
            "liveness failed to preserve a register across manual stop/resume");
  }

  {
    IrOp interaction = halt();
    interaction.meta.manual_interaction = ManualInteractionAnchor{
        .protocol_id = 1,
        .phase = 0,
        .kind = ManualInteractionAnchorKind::PromptStop,
    };
    const std::vector<IrOp> program = {store("1"), interaction, recall("1")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("1"),
            "manual interaction lost a register needed after resume");
    require(!info.live_in.at(1).contains("e"),
            "typed manual interaction incorrectly became an all-register memory barrier");
  }

  {
    const std::vector<IrOp> program = {store("4"), indirect_store("0", std::vector<int>{4, 5}),
                                       recall("4")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("4"),
            "a multi-target indirect store incorrectly killed every possible target");
    require(core::passes::register_effects(program.at(1)).may_defs.contains("4"),
            "multi-target indirect store was not represented as a may-def");
  }

  {
    const std::vector<IrOp> program = {store("4"), indirect_store("0", std::vector<int>{4}),
                                       recall("4")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.live_in.at(1).contains("4"),
            "a singleton indirect store failed to kill its proven target");
    require(core::passes::register_effects(program.at(1)).must_defs.contains("4"),
            "typed singleton indirect-memory metadata was not represented as a must-def");
    require(core::passes::known_indirect_memory_target(program.at(1)) == "4",
            "typed singleton indirect-memory metadata was ignored by the singleton helper");
  }

  {
    const core::passes::RegisterEffects mutating_memory =
        core::passes::register_effects(indirect_recall("4", std::vector<int>{8}));
    const core::passes::RegisterEffects mutating_flow =
        core::passes::register_effects(indirect_jump("6"));
    const core::passes::RegisterEffects stable_memory =
        core::passes::register_effects(indirect_recall("7", std::vector<int>{8}));

    require(mutating_memory.uses.contains("4") && mutating_memory.must_defs.contains("4"),
            "pre-increment indirect-memory selector was not modeled as use plus definition");
    require(mutating_flow.uses.contains("6") && mutating_flow.must_defs.contains("6"),
            "mutating indirect-flow selector was not modeled as use plus definition");
    require(stable_memory.uses.contains("7") && !stable_memory.must_defs.contains("7"),
            "stable R7 indirect selector was incorrectly modeled as a definition");
  }

  {
    IrOp logical_mutating = indirect_recall("selector", std::vector<int>{8});
    logical_mutating.opcode = 0xd4;
    logical_mutating.meta.logical_register_analysis = true;
    IrOp logical_stable = logical_mutating;
    logical_stable.opcode = 0xd7;

    require(core::passes::register_effects(logical_mutating).must_defs.contains("selector"),
            "logical liveness ignored the mutating class encoded by an indirect opcode");
    require(!core::passes::register_effects(logical_stable).must_defs.contains("selector"),
            "logical liveness treated a stable indirect opcode as selector mutation");
  }

  {
    const std::vector<IrOp> program = {indirect_recall("0", std::nullopt)};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(0).contains("e"),
            "unknown indirect recall did not conservatively use every machine register");
  }

  {
    IrOp discarded = indirect_recall("0", std::nullopt);
    discarded.meta.discarded_indirect_recall_value = true;
    const core::passes::RegisterEffects effects = core::passes::register_effects(discarded);

    require(effects.uses.contains("0") && effects.must_defs.contains("0"),
            "discarded indirect recall lost its mutating selector effect");
    require(!effects.uses_all_registers && !effects.uses.contains("e"),
            "discarded indirect recall incorrectly kept its unobserved memory value live");
  }

  {
    const std::vector<IrOp> program = {store("e"), indirect_store("0", std::nullopt), recall("e")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_in.at(1).contains("e"),
            "unknown indirect store incorrectly killed a possible old target value");
    require(core::passes::register_effects(program.at(1)).may_define_any_register,
            "unknown indirect store was not represented as an all-register may-def");
  }

  {
    const std::vector<IrOp> program = {indirect_jump("7"), recall("2")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.control_flow_targets_are_exact,
            "unknown indirect jump was reported as exact control flow");
    require(info.conservative_flow_sources == std::vector<int>{0},
            "unknown indirect jump did not identify its conservative source");
    require(info.live_in.at(0).size() >= 15U && info.live_in.at(0).contains("e"),
            "unknown indirect jump did not form a full-register liveness barrier");
  }

  {
    const std::vector<IrOp> program = {jump_to("missing"), recall("2")};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.control_flow_targets_are_exact,
            "unresolved direct target was reported as exact control flow");
    require(info.live_in.at(0).contains("e"),
            "unresolved direct target did not form a full-register liveness barrier");
  }

  {
    std::vector<IrOp> program{recall("e")};
    for (int index = 1; index < 260; ++index) {
      const int previous_address = index == 1 ? 0 : 1 + (2 * (index - 2));
      program.push_back(jump_to_address(previous_address));
    }
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.control_flow_targets_are_exact,
            "resolved long reverse graph was unexpectedly marked conservative");
    require(info.live_in.back().contains("e"),
            "liveness stopped before the fixed point on a graph needing over 200 propagations");
  }
}

} // namespace mkpro::tests
