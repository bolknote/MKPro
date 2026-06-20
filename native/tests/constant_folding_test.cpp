#include "mkpro/core/passes/constant_folding.hpp"

#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp plain(int opcode, std::string mnemonic, bool raw = false) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  op.meta.raw = raw;
  return op;
}

IrOp stop() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

core::passes::PassResult run_constant_folding(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::constant_folding(ops, core::passes::PassContext{.options = options});
}

} // namespace

void constant_folding_matches_typescript_contract() {
  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0"), plain(0x10, "+"), stop()});

    require(result.applied == 1, "constant-folding did not drop identity 0+ pair");
    require(result.ops.size() == 1, "constant-folding produced wrong op count for 0+");
    require(result.ops.at(0).kind == IrKind::Stop, "constant-folding dropped wrong op for 0+");
    require(result.optimizations.size() == 1, "constant-folding did not report optimization");
    require(result.optimizations.at(0).name == "constant-folding",
            "constant-folding reported wrong optimization name");
    require(result.optimizations.at(0).detail ==
                "Dropped 1 identity arithmetic operation(s) (0+ or 1*).",
            "constant-folding reported wrong optimization detail");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x01, "1"), plain(0x12, "*"), stop()});

    require(result.applied == 1, "constant-folding did not drop identity 1* pair");
    require(result.ops.size() == 1, "constant-folding produced wrong op count for 1*");
    require(result.ops.at(0).kind == IrKind::Stop, "constant-folding dropped wrong op for 1*");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0"), plain(0x11, "-"), stop()});

    require(result.applied == 0, "constant-folding incorrectly folded 0-");
    require(result.ops.size() == 3, "constant-folding changed non-identity subtraction");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x01, "1"), plain(0x13, "/"), stop()});

    require(result.applied == 0, "constant-folding incorrectly folded 1/");
    require(result.ops.size() == 3, "constant-folding changed non-identity division");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0", true), plain(0x10, "+"), stop()});

    require(result.applied == 0, "constant-folding ignored raw literal barrier");
    require(result.ops.size() == 3, "constant-folding changed raw literal sequence");
  }

  {
    const core::passes::PassResult result =
        run_constant_folding({plain(0x00, "0"), plain(0x10, "+", true), stop()});

    require(result.applied == 0, "constant-folding ignored raw arithmetic barrier");
    require(result.ops.size() == 3, "constant-folding changed raw arithmetic sequence");
  }

  {
    const core::passes::PassResult result = run_constant_folding(
        {plain(0x00, "0"), plain(0x10, "+"), plain(0x01, "1"), plain(0x12, "*"), stop()});

    require(result.applied == 2, "constant-folding did not fold both identity pairs");
    require(result.ops.size() == 1, "constant-folding produced wrong op count for two folds");
  }
}

} // namespace mkpro::tests
