#include "mkpro/core/passes/dead_store_before_commutative.hpp"

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

IrOp store(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = std::move(register_name);
  op.opcode = 0x40;
  op.meta.mnemonic = "X->П";
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

IrOp halt() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

core::passes::PassResult run_dead_temp_store(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::dead_store_before_commutative(
      ops, core::passes::PassContext{.options = options});
}

} // namespace

void dead_store_before_commutative_matches_typescript_contract() {
  {
    const core::passes::PassResult result =
        run_dead_temp_store({store("2"), recall("2"), plain(0x10, "+")});

    require(result.applied == 1, "dead-temp-store did not remove stack-consumed store");
    require(result.ops.size() == 2, "dead-temp-store produced wrong op count");
    require(result.ops.at(0).kind == IrKind::Recall,
            "dead-temp-store removed the recall instead of the store");
    require(result.optimizations.size() == 1,
            "dead-temp-store did not report optimization");
    require(result.optimizations.at(0).name == "dead-temp-store",
            "dead-temp-store reported wrong optimization name");
  }

  {
    const core::passes::PassResult result = run_dead_temp_store(
        {store("2"), recall("2"), plain(0x10, "+"), recall("2"), halt()});

    require(result.applied == 0, "dead-temp-store removed store before a later read");
    require(result.ops.size() == 5, "dead-temp-store changed later-read program");
  }

  {
    const core::passes::PassResult result = run_dead_temp_store(
        {store("2"), recall("2"), plain(0x12, "*"), store("2"), halt()});

    require(result.applied == 1,
            "dead-temp-store did not remove store before next same-register write");
    require(result.ops.size() == 4, "dead-temp-store next-write case produced wrong op count");
  }

  {
    const core::passes::PassResult result =
        run_dead_temp_store({store("2"), recall("2"), plain(0x10, "+"), halt()});

    require(result.applied == 0, "dead-temp-store ignored terminal stop barrier");
    require(result.ops.size() == 4, "dead-temp-store changed stop-barrier program");
  }

  {
    const core::passes::PassResult result = run_dead_temp_store(
        {store("2"), recall("2"), plain(0x10, "+"), jump_to("tail"), label("tail"),
         halt()});

    require(result.applied == 0, "dead-temp-store crossed a flow-control barrier");
    require(result.ops.size() == 6, "dead-temp-store changed flow-barrier program");
  }
}

} // namespace mkpro::tests
