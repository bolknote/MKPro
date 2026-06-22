#include "mkpro/core/passes/recall_removal.hpp"

#include "mkpro/core/passes/helpers.hpp"
#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp store(std::string register_name) {
  return make_store(std::move(register_name));
}

IrOp recall(std::string register_name) {
  return make_recall(std::move(register_name));
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

core::passes::PassResult run_simple_recall_removal(const std::vector<IrOp>& ops) {
  return core::passes::run_recall_removal_pass(
      ops,
      core::passes::RecallRemovalReport{
          .name = "test-recall-removal",
          .detail = [](int count) { return "removed " + std::to_string(count) + " recall(s)"; },
      },
      [](core::passes::RecallRemovalEngine& engine) {
        for (std::size_t index = 0; index < engine.ops().size(); ++index) {
          const std::optional<core::passes::RecallRemovalStackSchedulerPlan> plan =
              engine.plan(static_cast<int>(index));
          if (plan.has_value() && plan->removable)
            engine.removed().insert(static_cast<int>(index));
        }
      });
}

} // namespace

void recall_removal_engine_matches_initial_typescript_contract() {
  {
    const core::passes::PassResult result =
        run_simple_recall_removal({store("1"), recall("1"), halt()});

    require(result.applied == 1, "recall-removal did not remove dead standalone recall");
    require(result.ops.size() == 2, "recall-removal standalone result size mismatch");
    require(result.ops.at(0).kind == IrKind::Store, "recall-removal removed the producer store");
    require(result.optimizations.size() == 1,
            "recall-removal did not report through shared driver");
    require(result.optimizations.at(0).name == "test-recall-removal",
            "recall-removal reported wrong pass name");
  }

  {
    const core::passes::PassResult result =
        run_simple_recall_removal({store("1"), recall("1"), plain(0x10, "+"), halt()});

    require(result.applied == 0, "recall-removal removed recall whose stack lift feeds +");
    require(result.ops.size() == 4, "recall-removal changed stack-consuming program");
  }

  {
    const core::passes::PassResult result =
        run_simple_recall_removal({store("1"), recall("1"), plain(0x0c, "ВП"), halt()});

    require(result.applied == 0, "recall-removal removed recall before context-sensitive ВП");
    require(result.ops.size() == 4, "recall-removal changed X2-restore-sensitive program");
  }

  {
    const std::vector<IrOp> program = {store("1"),     recall("1"), recall("2"), recall("3"),
                                       plain(0x10, "+"), plain(0x10, "+"), plain(0x10, "+"),
                                       halt()};

    require(core::passes::removing_recall_can_expose_stack_lift(program, 1),
            "recall-removal lost TS depth-3 stack-difference safety");
  }
}

} // namespace mkpro::tests
