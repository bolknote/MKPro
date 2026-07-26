#include "mkpro/core/passes/x2_hidden_temp_restore.hpp"

#include "mkpro/core/ir.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

void x2_preloaded_constant_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::x2_hidden_temp_restore_pass().run(program, ctx);
  };
  const auto is_dot = [](const IrOp& op) {
    return op.kind == IrKind::Plain && op.opcode == 0x0a;
  };

  {
    const std::vector<IrOp> program = {
        recall("1", "preload const 2"),
        plain(0x0e, "В↑"),
        plain(0x14, "<->"),
        plain(0x14, "<->"),
        recall("1", "preload const 2"),
        halt(),
    };
    const auto result = run(program);
    require(result.applied == 1,
            "preloaded decimal: expected the dead recall to use dot restore");
    require(is_dot(result.ops.at(4)),
            "preloaded decimal: expected dot restore at the second recall");
  }

  {
    const std::vector<IrOp> program = {
        recall("1", "preload const 2"),
        plain(0x0e, "В↑"),
        plain(0x14, "<->"),
        indirect_store("8"),
        plain(0x54, "К НОП"),
        recall("1", "preload const 2"),
        halt(),
    };
    const auto result = run(program);
    require(result.applied == 0,
            "unknown indirect store must invalidate a preloaded-register proof");
    require(result.ops.at(5).kind == IrKind::Recall,
            "unknown indirect store must preserve the recall");
  }

  {
    const std::vector<IrOp> program = {
        recall("1", "preload const 2"),
        plain(0x0e, "В↑"),
        plain(0x14, "<->"),
        known_target_indirect_store("8", "2"),
        plain(0x54, "К НОП"),
        recall("1", "preload const 2"),
        halt(),
    };
    const auto result = run(program);
    require(result.applied == 1,
            "known store to another register should preserve the preload proof");
    require(is_dot(result.ops.at(5)),
            "known store to another register should allow dot restore");
  }

  {
    const std::vector<IrOp> program = {
        jump("main"),
        label("overwrite"),
        plain(0x03, "3"),
        store("1"),
        ret(),
        label("main"),
        recall("1", "preload const 2"),
        plain(0x0e, "В↑"),
        plain(0x14, "<->"),
        plain(0x14, "<->"),
        call("overwrite"),
        recall("1", "preload const 2"),
        halt(),
    };
    const auto result = run(program);
    require(result.applied == 0,
            "direct helper write must invalidate a preloaded-register proof");
    require(result.ops.at(11).kind == IrKind::Recall,
            "direct helper write must preserve the recall");
  }

  {
    const std::vector<IrOp> program = {
        jump("main"),
        orphan_address(2),
        plain(0x03, "3"),
        store("1"),
        ret(),
        label("main"),
        recall("1", "preload const 2"),
        plain(0x0e, "В↑"),
        plain(0x14, "<->"),
        plain(0x14, "<->"),
        known_target_indirect_call("7", 2),
        recall("1", "preload const 2"),
        halt(),
    };
    const auto result = run(program);
    require(result.applied == 0,
            "indirect helper write must invalidate a preloaded-register proof");
    require(result.ops.at(11).kind == IrKind::Recall,
            "indirect helper write must preserve the recall");
  }

  {
    const std::vector<IrOp> program = {
        recall("1", "preload const FACE"),
        plain(0x0e, "В↑"),
        plain(0x14, "<->"),
        plain(0x14, "<->"),
        recall("1", "preload const FACE"),
        halt(),
    };
    const auto result = run(program);
    require(result.applied == 0,
            "structural preload without decimal value proof must keep the recall");
  }
}

} // namespace mkpro::tests
