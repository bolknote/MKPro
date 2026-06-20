#include "mkpro/core/passes/arithmetic_if.hpp"

#include "test_support.hpp"

#include <algorithm>
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

IrOp jump(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  return op;
}

IrOp cjump(std::string target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.opcode = 0x5e;
  op.target = std::move(target);
  op.meta.mnemonic = "F x=0";
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
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

core::passes::PassResult run_arithmetic_if(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::arithmetic_if(ops, core::passes::PassContext{.options = options});
}

} // namespace

void arithmetic_if_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {
        cjump("else"), plain(0x01, "1"), halt(), jump("end"),
        label("else"), plain(0x01, "1"), halt(), label("end"),
    };
    const core::passes::PassResult result = run_arithmetic_if(program);

    require(result.applied == 1,
            "arithmetic-if-pass did not collapse byte-identical simplified branches");
    require(std::none_of(result.ops.begin(), result.ops.end(),
                         [](const IrOp& op) { return op.kind == IrKind::CondJump; }),
            "arithmetic-if-pass left the redundant conditional jump");
    require(std::count_if(result.ops.begin(), result.ops.end(),
                          [](const IrOp& op) { return op.kind == IrKind::Stop; }) == 1,
            "arithmetic-if-pass did not keep exactly one terminal stop");
    require(result.optimizations.size() == 1,
            "arithmetic-if-pass did not report optimization metadata");
    require(result.optimizations.at(0).name == "arithmetic-if-pass",
            "arithmetic-if-pass reported wrong optimization name");
  }

  {
    const std::vector<IrOp> program = {
        cjump("else"), plain(0x01, "1"), halt(), jump("end"),
        label("else"), plain(0x02, "2"), halt(), label("end"),
    };
    const core::passes::PassResult result = run_arithmetic_if(program);

    require(result.applied == 0, "arithmetic-if-pass collapsed branches with different effects");
    require(result.ops.size() == program.size(),
            "arithmetic-if-pass changed non-equivalent branches");
  }

  {
    const std::vector<IrOp> program = {
        cjump("else"),    plain(0x01, "1"), halt(),       jump("end"),  label("else"),
        plain(0x01, "1"), halt(),           jump("else"), label("end"),
    };
    const core::passes::PassResult result = run_arithmetic_if(program);

    require(result.applied == 0, "arithmetic-if-pass ignored extra false-label reference");
    require(result.ops.size() == program.size(),
            "arithmetic-if-pass changed multi-reference false branch");
  }
}

} // namespace mkpro::tests
