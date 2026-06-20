#include "mkpro/core/passes/dead_code_after_halt.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
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

IrOp known_target_indirect_jump(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(selector);
  op.opcode = 0x87;
  op.meta.mnemonic = "К БП 7";
  op.meta.comment = "preloaded R7=C5 indirect-target=" + std::to_string(target) +
                    " indirect flow";
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

core::passes::PassResult run_dead_code_after_halt(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::dead_code_after_halt(ops,
                                            core::passes::PassContext{.options = options});
}

bool has_plain_opcode(const std::vector<IrOp>& ops, int opcode) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    return op.kind == IrKind::Plain && op.opcode == opcode;
  });
}

bool has_stop(const std::vector<IrOp>& ops) {
  return std::any_of(ops.begin(), ops.end(),
                     [](const IrOp& op) { return op.kind == IrKind::Stop; });
}

} // namespace

void dead_code_after_halt_matches_typescript_contract() {
  {
    const core::passes::PassResult result =
        run_dead_code_after_halt({jump_to("END"), plain(0x00, "0"), plain(0x10, "+"),
                                  label("END"), halt()});
    require(result.applied >= 2, "dead-code-after-halt did not prune skipped entry ops");
    require(!has_plain_opcode(result.ops, 0x00), "dead-code-after-halt kept skipped 0");
    require(!has_plain_opcode(result.ops, 0x10), "dead-code-after-halt kept skipped +");
  }

  {
    const core::passes::PassResult result =
        run_dead_code_after_halt({known_target_indirect_jump("7", 4), plain(0x09, "9"),
                                  plain(0x08, "8"), plain(0x07, "7"), halt()});
    require(has_stop(result.ops), "dead-code-after-halt dropped known indirect jump target");
    require(!has_plain_opcode(result.ops, 0x09),
            "dead-code-after-halt kept indirect-jump fallthrough");
  }

  {
    const core::passes::PassResult result =
        run_dead_code_after_halt({call_to("terminal"), plain(0x09, "9"),
                                  label("terminal"), halt()});
    require(!has_plain_opcode(result.ops, 0x09),
            "dead-code-after-halt kept non-returning call continuation");
    require(result.applied >= 1,
            "dead-code-after-halt did not count non-returning call continuation removal");
  }

  {
    const core::passes::PassResult result =
        run_dead_code_after_halt({call_to("returns"), plain(0x09, "9"), halt(),
                                  label("returns"), ret()});
    require(has_plain_opcode(result.ops, 0x09),
            "dead-code-after-halt removed continuation reached by return");
  }
}

} // namespace mkpro::tests
