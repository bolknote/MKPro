#include "mkpro/core/passes/pre_shift_stack_lift.hpp"

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (pre-shift-stack-lift describe block; 70
//   it(...) blocks across both regions). Every preShiftStackLift.run(...)
//   assertion is ported here 1:1. Failures are collected and checked against an
//   (initially empty) deferred set via the divergence-ratchet pattern.

void pre_shift_stack_lift_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};

  std::vector<std::string> failures;
  const auto fail = [&](const std::string& label) { failures.push_back(label); };
  const auto run = [&](const std::vector<IrOp>& p) {
    return core::passes::pre_shift_stack_lift_pass().run(p, ctx);
  };
  const auto call_op = [](std::string target, int opcode, std::string mnemonic,
                          std::string comment) {
    IrOp op;
    op.kind = IrKind::Call;
    op.target = std::move(target);
    op.opcode = opcode;
    op.meta.mnemonic = std::move(mnemonic);
    op.meta.comment = std::move(comment);
    return op;
  };
  const auto check_applied = [&](int actual, int expected, const std::string& label) {
    if (actual != expected)
      fail(label);
  };
  const auto check_ops_equal = [&](const std::vector<IrOp>& a, const std::vector<IrOp>& b,
                                   const std::string& label) {
    if (mkpro::ir_ops_to_json(a) != mkpro::ir_ops_to_json(b))
      fail(label);
  };

  {
    // pre-shift-stack-lift removes В↑ already supplied by following recall
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ already supplied by following recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ already supplied by following recall");
  }

  {
    // pre-shift-stack-lift removes В↑ before terminal halt
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before terminal halt");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      halt(),
    }), "pre-shift-stack-lift removes В↑ before terminal halt");
  }

  {
    // pre-shift-stack-lift keeps В↑ before resumable pause
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      pause(),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before resumable pause");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before resumable pause");
  }

  {
    // pre-shift-stack-lift removes В↑ after recall when the recall already supplies X2 sync
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after recall when the recall already supplies X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after recall when the recall already supplies X2 sync");
  }

  {
    // pre-shift-stack-lift removes В↑ after an existing stack lift and X2 sync
    const IrOp firstLift = plain_with_comment(0x0e, "В↑", "first lift");
    const IrOp secondLift = plain_with_comment(0x0e, "В↑", "second lift");
    const std::vector<IrOp> program = {
      firstLift,
      secondLift,
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after an existing stack lift and X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      firstLift,
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after an existing stack lift and X2 sync");
  }

  {
    // pre-shift-stack-lift keeps post-recall В↑ when its stack lift is consumed
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-recall В↑ when its stack lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-recall В↑ when its stack lift is consumed");
  }

  {
    // pre-shift-stack-lift keeps post-recall В↑ after display-sensitive recalls
    const std::vector<IrOp> program = {
      recall("1", "display digit"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-recall В↑ after display-sensitive recalls");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-recall В↑ after display-sensitive recalls");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through transparent stack/X2 gaps
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x54, "К НОП"),
      label("local_gap"),
      store("2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through transparent stack/X2 gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x54, "К НОП"),
      label("local_gap"),
      store("2"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through transparent stack/X2 gaps");
  }

  {
    // pre-shift-stack-lift removes post-helper В↑ after a direct-return stack/X2 producer
    const std::vector<IrOp> program = {
      jump("main"),
      label("load"),
      recall("1"),
      ret(),
      label("main"),
      call("load"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-helper В↑ after a direct-return stack/X2 producer");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("load"),
      recall("1"),
      ret(),
      label("main"),
      call("load"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes post-helper В↑ after a direct-return stack/X2 producer");
  }

  {
    // pre-shift-stack-lift keeps post-helper В↑ after a direct-return producer when its lift is consumed
    const std::vector<IrOp> program = {
      jump("main"),
      label("load"),
      recall("1"),
      ret(),
      label("main"),
      call("load"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-helper В↑ after a direct-return producer when its lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-helper В↑ after a direct-return producer when its lift is consumed");
  }

  {
    // pre-shift-stack-lift keeps post-producer В↑ through gaps when its stack lift is consumed
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-producer В↑ through gaps when its stack lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-producer В↑ through gaps when its stack lift is consumed");
  }

  {
    // pre-shift-stack-lift stops post-producer scanning at stack-consuming gaps
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift stops post-producer scanning at stack-consuming gaps");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift stops post-producer scanning at stack-consuming gaps");
  }

  {
    // pre-shift-stack-lift stops post-producer scanning at X-changing gaps
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift stops post-producer scanning at X-changing gaps");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift stops post-producer scanning at X-changing gaps");
  }

  {
    // pre-shift-stack-lift stops post-producer scanning at targeted entry labels
    const std::vector<IrOp> program = {
      recall("1"),
      label("entry"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      jump("entry"),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift stops post-producer scanning at targeted entry labels");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift stops post-producer scanning at targeted entry labels");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through direct conditional fallthrough
    const std::vector<IrOp> program = {
      recall("1"),
      cjump("done"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through direct conditional fallthrough");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through direct conditional fallthrough");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through counted-loop fallthrough
    const std::vector<IrOp> program = {
      recall("1"),
      loop("done"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through counted-loop fallthrough");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      loop("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through counted-loop fallthrough");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through proved indirect conditional fallthrough
    const std::vector<IrOp> program = {
      recall("1"),
      known_target_indirect_cjump("7", 5),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through proved indirect conditional fallthrough");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      known_target_indirect_cjump("7", 5),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through proved indirect conditional fallthrough");
  }

  {
    // pre-shift-stack-lift keeps post-producer В↑ across unknown indirect conditionals
    const std::vector<IrOp> program = {
      recall("1"),
      indirect_cjump("7"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-producer В↑ across unknown indirect conditionals");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-producer В↑ across unknown indirect conditionals");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through X-preserving direct-return helpers
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      store("2"),
      ret(),
      label("main"),
      recall("1"),
      call("noop"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through X-preserving direct-return helpers");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      store("2"),
      ret(),
      label("main"),
      recall("1"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through X-preserving direct-return helpers");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through proved indirect-return helpers
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      recall("1"),
      known_target_indirect_call("7", 2),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through proved indirect-return helpers");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      recall("1"),
      known_target_indirect_call("7", 2),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through proved indirect-return helpers");
  }

  {
    // pre-shift-stack-lift removes post-producer В↑ through nested X-preserving direct-return helpers
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes post-producer В↑ through nested X-preserving direct-return helpers");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes post-producer В↑ through nested X-preserving direct-return helpers");
  }

  {
    // pre-shift-stack-lift removes В↑ before a path-safe fallthrough X2 sync
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a path-safe fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a path-safe fallthrough X2 sync");
  }

  {
    // pre-shift-stack-lift removes В↑ through a proved indirect gap before a fallthrough X2 sync
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      known_target_indirect_cjump("7", 8),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ through a proved indirect gap before a fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_cjump("7", 8),
      cjump("done"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ through a proved indirect gap before a fallthrough X2 sync");
  }

  {
    // pre-shift-stack-lift keeps В↑ before a proved indirect gap when the jump edge observes X2
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      known_target_indirect_cjump("7", 5),
      halt(),
      label("restore"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before a proved indirect gap when the jump edge observes X2");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before a proved indirect gap when the jump edge observes X2");
  }

  {
    // pre-shift-stack-lift removes В↑ before a plain X-preserving X2 sync
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a plain X-preserving X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a plain X-preserving X2 sync");
  }

  {
    // pre-shift-stack-lift keeps В↑ before a plain X2 sync when the lift is consumed
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0xf0, "F* empty F0"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before a plain X2 sync when the lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before a plain X2 sync when the lift is consumed");
  }

  {
    // pre-shift-stack-lift removes В↑ after a plain X-preserving X2 sync
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after a plain X-preserving X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after a plain X-preserving X2 sync");
  }

  {
    // pre-shift-stack-lift removes В↑ after a hard X/X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after a hard X/X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x0d, "Cx"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after a hard X/X2 overwrite");
  }

  {
    // pre-shift-stack-lift keeps В↑ after a hard X/X2 overwrite when the lift is consumed
    const std::vector<IrOp> program = {
      plain(0x0d, "Cx"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ after a hard X/X2 overwrite when the lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ after a hard X/X2 overwrite when the lift is consumed");
  }

  {
    // pre-shift-stack-lift keeps В↑ after a plain X2 sync when the lift is consumed
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ after a plain X2 sync when the lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ after a plain X2 sync when the lift is consumed");
  }

  {
    // pre-shift-stack-lift removes В↑ after a direct fallthrough X2 sync
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      cjump("done"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after a direct fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      cjump("done"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after a direct fallthrough X2 sync");
  }

  {
    // pre-shift-stack-lift keeps В↑ after a conditional when the jump edge enters the lift
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      cjump("entry"),
      label("entry"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ after a conditional when the jump edge enters the lift");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ after a conditional when the jump edge enters the lift");
  }

  {
    // pre-shift-stack-lift removes В↑ after a transparent return-call X2 sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("noop"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after a transparent return-call X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("noop"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after a transparent return-call X2 sync");
  }

  {
    // pre-shift-stack-lift removes В↑ after a stack-preserving return helper that changes X
    const std::vector<IrOp> program = {
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      recall("1"),
      call("square"),
      plain(0x54, "К НОП"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ after a stack-preserving return helper that changes X");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      recall("1"),
      call("square"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ after a stack-preserving return helper that changes X");
  }

  {
    // pre-shift-stack-lift stops post-plain-sync scanning at X-changing gaps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift stops post-plain-sync scanning at X-changing gaps");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift stops post-plain-sync scanning at X-changing gaps");
  }

  {
    // pre-shift-stack-lift keeps В↑ before a fallthrough sync when the jump edge observes X2
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      cjump("restore"),
      halt(),
      label("restore"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before a fallthrough sync when the jump edge observes X2");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before a fallthrough sync when the jump edge observes X2");
  }

  {
    // pre-shift-stack-lift keeps post-producer В↑ before nested helpers that restore X2
    const std::vector<IrOp> program = {
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("restore"),
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-producer В↑ before nested helpers that restore X2");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-producer В↑ before nested helpers that restore X2");
  }

  {
    // pre-shift-stack-lift keeps post-producer В↑ before display-sensitive nested helper calls
    const IrOp displayCall = call_op("noop", 0x53, "ПП", "display helper call");
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      displayCall,
      ret(),
      label("main"),
      recall("1"),
      call("outer"),
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps post-producer В↑ before display-sensitive nested helper calls");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps post-producer В↑ before display-sensitive nested helper calls");
  }

  {
    // pre-shift-stack-lift keeps В↑ after helpers that change X when the lift is consumed
    const std::vector<IrOp> program = {
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      recall("1"),
      call("square"),
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ after helpers that change X when the lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ after helpers that change X when the lift is consumed");
  }

  {
    // pre-shift-stack-lift keeps В↑ before context-sensitive X2 restore gaps
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x0a, "."),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before context-sensitive X2 restore gaps");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before context-sensitive X2 restore gaps");
  }

  {
    // pre-shift-stack-lift removes В↑ before indirect recall
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      indirect_recall("7"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before indirect recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      indirect_recall("7"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before indirect recall");
  }

  {
    // pre-shift-stack-lift removes В↑ before F pi when the following constant push supplies Y
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x20, "F pi"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before F pi when the following constant push supplies Y");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x20, "F pi"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before F pi when the following constant push supplies Y");
  }

  {
    // pre-shift-stack-lift skips stack-preserving gap ops before the producer
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      label("marker"),
      store("2"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift skips stack-preserving gap ops before the producer");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x54, "К НОП"),
      label("marker"),
      store("2"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift skips stack-preserving gap ops before the producer");
  }

  {
    // pre-shift-stack-lift crosses direct conditional fallthrough when the other edge cannot observe stack or X2
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      cjump("done"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses direct conditional fallthrough when the other edge cannot observe stack or X2");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      cjump("done"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift crosses direct conditional fallthrough when the other edge cannot observe stack or X2");
  }

  {
    // pre-shift-stack-lift crosses counted-loop fallthrough before a hard X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      loop("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses counted-loop fallthrough before a hard X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      loop("done"),
      plain(0x0d, "Cx"),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift crosses counted-loop fallthrough before a hard X2 overwrite");
  }

  {
    // pre-shift-stack-lift crosses simple direct-return callees before a following recall
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses simple direct-return callees before a following recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      call("noop"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift crosses simple direct-return callees before a following recall");
  }

  {
    // pre-shift-stack-lift removes В↑ before a direct-return stack/X2 producer
    const std::vector<IrOp> program = {
      jump("main"),
      label("load"),
      recall("1"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("load"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a direct-return stack/X2 producer");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("load"),
      recall("1"),
      ret(),
      label("main"),
      call("load"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a direct-return stack/X2 producer");
  }

  {
    // pre-shift-stack-lift keeps В↑ before direct-return producers with earlier stack consumers
    const std::vector<IrOp> program = {
      jump("main"),
      label("consume"),
      plain(0x10, "+"),
      recall("1"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("consume"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before direct-return producers with earlier stack consumers");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before direct-return producers with earlier stack consumers");
  }

  {
    // pre-shift-stack-lift crosses nested direct-return callees before a following recall
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("outer"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses nested direct-return callees before a following recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      call("outer"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift crosses nested direct-return callees before a following recall");
  }

  {
    // pre-shift-stack-lift crosses known indirect-return callees before a following recall
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      known_target_indirect_call("7", 2),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses known indirect-return callees before a following recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      known_target_indirect_call("7", 2),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift crosses known indirect-return callees before a following recall");
  }

  {
    // pre-shift-stack-lift crosses proved indirect conditional fallthrough before a following recall
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      known_target_indirect_cjump("7", 5),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses proved indirect conditional fallthrough before a following recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      known_target_indirect_cjump("7", 5),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("done"),
      halt(),
    }), "pre-shift-stack-lift crosses proved indirect conditional fallthrough before a following recall");
  }

  {
    // pre-shift-stack-lift keeps lifts across proved indirect conditionals when the target consumes stack
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      known_target_indirect_cjump("7", 5),
      recall("1"),
      plain(0x12, "*"),
      halt(),
      label("consume"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps lifts across proved indirect conditionals when the target consumes stack");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps lifts across proved indirect conditionals when the target consumes stack");
  }

  {
    // pre-shift-stack-lift crosses simple direct-return callees before hard X2 overwrite
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift crosses simple direct-return callees before hard X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      call("noop"),
      plain(0x0d, "Cx"),
      halt(),
    }), "pre-shift-stack-lift crosses simple direct-return callees before hard X2 overwrite");
  }

  {
    // pre-shift-stack-lift removes В↑ before a transparent direct-return X2 sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a transparent direct-return X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      call("noop"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a transparent direct-return X2 sync");
  }

  {
    // pre-shift-stack-lift removes В↑ before a stack-preserving return helper that changes X
    const std::vector<IrOp> program = {
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a stack-preserving return helper that changes X");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a stack-preserving return helper that changes X");
  }

  {
    // pre-shift-stack-lift removes В↑ before a return X2 sync through an X-changing gap
    const std::vector<IrOp> program = {
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x22, "F x^2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a return X2 sync through an X-changing gap");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("square"),
      plain(0x22, "F x^2"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x22, "F x^2"),
      call("square"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a return X2 sync through an X-changing gap");
  }

  {
    // pre-shift-stack-lift removes В↑ before a transparent known-indirect return X2 sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      known_target_indirect_call("7", 2),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a transparent known-indirect return X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      known_target_indirect_call("7", 2),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a transparent known-indirect return X2 sync");
  }

  {
    // pre-shift-stack-lift removes В↑ before a linear В/О X2 sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("sync"),
      plain(0x0e, "В↑"),
      ret(),
      label("main"),
      call("sync"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ before a linear В/О X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("sync"),
      ret(),
      label("main"),
      call("sync"),
      plain(0x0a, "."),
      halt(),
    }), "pre-shift-stack-lift removes В↑ before a linear В/О X2 sync");
  }

  {
    // pre-shift-stack-lift keeps В↑ before В/О when a caller consumes the stack lift
    const std::vector<IrOp> program = {
      jump("main"),
      label("sync"),
      plain(0x0e, "В↑"),
      ret(),
      label("main"),
      call("sync"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before В/О when a caller consumes the stack lift");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before В/О when a caller consumes the stack lift");
  }

  {
    // pre-shift-stack-lift keeps В↑ before a direct-return sync when the lift reaches a stack consumer
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("noop"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before a direct-return sync when the lift reaches a stack consumer");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before a direct-return sync when the lift reaches a stack consumer");
  }

  {
    // pre-shift-stack-lift keeps В↑ before direct-return callees that consume stack
    const std::vector<IrOp> program = {
      jump("main"),
      label("consume"),
      plain(0x10, "+"),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("consume"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before direct-return callees that consume stack");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before direct-return callees that consume stack");
  }

  {
    // pre-shift-stack-lift keeps В↑ before direct-return callees that restore X2
    const std::vector<IrOp> program = {
      jump("main"),
      label("restore"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x0e, "В↑"),
      call("restore"),
      recall("1"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before direct-return callees that restore X2");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before direct-return callees that restore X2");
  }

  {
    // pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge observes X2
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      cjump("done"),
      recall("1"),
      halt(),
      label("done"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge observes X2");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge observes X2");
  }

  {
    // pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge consumes the stack lift
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      cjump("done"),
      recall("1"),
      halt(),
      label("done"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge consumes the stack lift");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ across conditionals when the skipped edge consumes the stack lift");
  }

  {
    // pre-shift-stack-lift stops gap scanning at stack-consuming commands
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x10, "+"),
      recall("1"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift stops gap scanning at stack-consuming commands");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift stops gap scanning at stack-consuming commands");
  }

  {
    // pre-shift-stack-lift removes В↑ made dead before a hard X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift removes В↑ made dead before a hard X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x54, "К НОП"),
      store("2"),
      plain(0x0d, "Cx"),
      halt(),
    }), "pre-shift-stack-lift removes В↑ made dead before a hard X2 overwrite");
  }

  {
    // pre-shift-stack-lift keeps В↑ before hard X2 overwrite when the stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x0d, "Cx"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before hard X2 overwrite when the stack lift is consumed");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before hard X2 overwrite when the stack lift is consumed");
  }

  {
    // pre-shift-stack-lift collapses adjacent В↑ lifts when the deeper duplicate is unused
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      plain(0x0e, "В↑"),
      plain(0x12, "*"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "pre-shift-stack-lift collapses adjacent В↑ lifts when the deeper duplicate is unused");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x0e, "В↑"),
      plain(0x12, "*"),
      halt(),
    }), "pre-shift-stack-lift collapses adjacent В↑ lifts when the deeper duplicate is unused");
  }

  {
    // pre-shift-stack-lift keeps В↑ when the deeper stack difference reaches a later consumer
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x12, "*"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ when the deeper stack difference reaches a later consumer");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ when the deeper stack difference reaches a later consumer");
  }

  {
    // pre-shift-stack-lift keeps В↑ before stack-exposing commands
    const std::vector<IrOp> program = {
      plain(0x0e, "В↑"),
      recall("1"),
      plain(0x25, "F reverse"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "pre-shift-stack-lift keeps В↑ before stack-exposing commands");
    check_ops_equal(result.ops, program, "pre-shift-stack-lift keeps В↑ before stack-exposing commands");
  }

  const std::set<std::string> deferred = {};

  std::set<std::string> failed(failures.begin(), failures.end());
  std::string message;
  for (const std::string& label : failed) {
    if (!deferred.contains(label))
      message += "\n  - unexpected divergence: " + label;
  }
  for (const std::string& label : deferred) {
    if (!failed.contains(label))
      message += "\n  - deferred case now passes (promote to covered): " + label;
  }
  require(message.empty(), "pre-shift-stack-lift parity divergence set changed:" + message);
}

} // namespace mkpro::tests
