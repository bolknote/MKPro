#include "mkpro/core/passes/liveness_analysis.hpp"

#include "test_support.hpp"

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
    const std::vector<IrOp> program = {call_to("terminal"), recall("3"),
                                       label("terminal"), halt()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(!info.live_out.at(0).contains("3"),
            "liveness propagated through non-returning direct call continuation");
  }

  {
    const std::vector<IrOp> program = {call_to("returns"), recall("3"), halt(),
                                       label("returns"), ret()};
    const core::passes::LivenessInfo info = core::passes::compute_liveness(program);

    require(info.live_out.at(0).contains("3"),
            "liveness did not propagate direct call continuation through return");
  }
}

} // namespace mkpro::tests
