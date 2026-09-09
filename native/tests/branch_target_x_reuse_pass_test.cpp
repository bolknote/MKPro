#include "mkpro/core/passes/branch_target_x_reuse.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/passes/flow_x_reuse.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (branch-target-x-reuse describe block, 30 cases)
//
// Cases depending on the not-yet-ported X2 decimal-digit-run / structural
// VP-source machinery are tracked in kDeferred and guarded by an exact
// divergence-set check (see last_x_reuse_test.cpp for rationale).

void branch_target_x_reuse_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::branch_target_x_reuse_pass().run(program, ctx);
  };
  std::vector<std::string> failures;
  const auto check_applied = [&](int actual, int expected, const std::string& label) {
    if (actual != expected)
      failures.push_back(label);
  };
  const auto check_ops = [&](const std::vector<IrOp>& a, const std::vector<IrOp>& e,
                             const std::string& label) {
    if (mkpro::ir_ops_to_json(a) != mkpro::ir_ops_to_json(e))
      failures.push_back(label);
  };
  const auto check = [&](bool ok, const std::string& label) {
    if (!ok)
      failures.push_back(label);
  };
  const auto count_recall = [](const std::vector<IrOp>& a, const std::string& reg) {
    int n = 0;
    for (const IrOp& op : a)
      if (op.kind == IrKind::Recall && op.register_name == reg)
        ++n;
    return n;
  };
  const auto count_indirect_recall = [](const std::vector<IrOp>& a) {
    int n = 0;
    for (const IrOp& op : a)
      if (op.kind == IrKind::IndirectRecall)
        ++n;
    return n;
  };
  const auto op_after_target_is_plain32 = [](const std::vector<IrOp>& a) {
    for (std::size_t index = 0; index + 1 < a.size(); ++index) {
      if (a.at(index).kind == IrKind::Label && a.at(index).name == "target")
        return a.at(index + 1).kind == IrKind::Plain && a.at(index + 1).opcode == 0x32;
    }
    return false;
  };

  {
    const std::vector<IrOp> program = {recall("6"),    cjump("target"), jump("end"),
                                       label("target"), recall("6"),     plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall when condition target preserves X");
    check(!result.optimizations.empty() &&
              result.optimizations.at(0).name == "branch-target-x-reuse",
          "condition target optimization name");
    check(count_recall(result.ops, "6") == 1, "condition target recall count");
    check(op_after_target_is_plain32(result.ops), "condition target op after label");
  }

  {
    const std::vector<IrOp> program = {recall("6"),    cjump("target"),
                                       halt(),         label("target"),
                                       recall("6"),    plain(0x32, "\u041a \u0417\u041d"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "treats stop as no-fallthrough target separator");
    check_ops(result.ops,
              {recall("6"), cjump("target"), halt(), label("target"),
               plain(0x32, "\u041a \u0417\u041d"), halt()},
              "stop separator target drop");
  }

  {
    const std::vector<IrOp> program = {recall("6"),    loop("target"),
                                       halt(),         label("target"),
                                       recall("6"),    plain(0x32, "\u041a \u0417\u041d"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall when counted-loop target preserves non-counter X");
    check_ops(result.ops,
              {recall("6"), loop("target"), halt(), label("target"),
               plain(0x32, "\u041a \u0417\u041d"), halt()},
              "counted-loop non-counter target drop");
  }

  {
    const std::vector<IrOp> program = {recall("0"),    loop("target"),
                                       halt(),         label("target"),
                                       recall("0"),    plain(0x32, "\u041a \u0417\u041d"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps counted-loop counter target recalls");
    check_ops(result.ops, program, "counted-loop counter target preserved");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_recall("7", "6"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       known_target_indirect_recall("8", "6"),
                                       plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops stable indirect recall when condition tested target");
    check(count_indirect_recall(result.ops) == 1, "stable indirect target recall count");
    check(op_after_target_is_plain32(result.ops), "stable indirect target op after label");
  }

  {
    const std::vector<IrOp> program = {recall("6"),
                                       known_target_indirect_cjump("8", 4),
                                       jump("end"),
                                       label("target"),
                                       recall("6"),
                                       plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "follows proved stable indirect conditional targets");
    check_ops(result.ops,
              {recall("6"), known_target_indirect_cjump("8", 4), jump("end"), label("target"),
               plain(0x32, "\u041a \u0417\u041d"), label("end"), halt()},
              "stable indirect conditional target drop");
  }

  {
    const std::vector<IrOp> program = {recall("1"),
                                       known_target_indirect_cjump("1", 4),
                                       jump("end"),
                                       label("target"),
                                       recall("1"),
                                       plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps proved indirect target recalls for mutating selectors");
    check_ops(result.ops, program, "mutating selector indirect target preserved");
  }

  {
    const std::vector<IrOp> program = {recall("6"),    cjump("target"), jump("end"),
                                       label("target"), recall("6"),     plain(0x0c, "\u0412\u041f"),
                                       label("end"),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall that syncs X2 before VP in target");
    check_ops(result.ops, program, "target recall before VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("6"),    cjump("target"),     jump("end"),
                                       label("target"), recall("6"),         plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), label("end"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops redundant target X2 sync before preserving op and VP");
    check_ops(result.ops,
              {recall("6"), cjump("target"), jump("end"), label("target"), plain(0x20, "F pi"),
               plain(0x0c, "\u0412\u041f"), label("end"), halt()},
              "redundant target X2 sync drop");
  }

  {
    const std::vector<IrOp> program = {recall("6"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       recall("6"),
                                       plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "crosses transparent target prefix before redundant recall");
    check_ops(result.ops,
              {recall("6"), cjump("target"), jump("end"), label("target"),
               plain(0x54, "\u041a\u041d\u041e\u041f"), plain(0x20, "F pi"),
               plain(0x0c, "\u0412\u041f"), label("end"), halt()},
              "transparent target prefix drop");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       ret(),
                                       label("main"),
                                       recall("6"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       call("transparent"),
                                       recall("6"),
                                       halt(),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "crosses transparent return-helper target prefixes");
    check_ops(result.ops,
              {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"), ret(),
               label("main"), recall("6"), cjump("target"), jump("end"), label("target"),
               call("transparent"), halt(), label("end"), halt()},
              "transparent return-helper target prefix drop");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("clobber"),
                                       plain(0x0d, "Cx"),
                                       ret(),
                                       label("main"),
                                       recall("6"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       call("clobber"),
                                       recall("6"),
                                       halt(),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps target recalls after nontransparent return-helper prefix");
    check_ops(result.ops, program, "nontransparent return-helper target preserved");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("save"),
                                       store("6"),
                                       ret(),
                                       label("main"),
                                       recall("4"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       call("save"),
                                       recall("6"),
                                       plain(0x35, "\u041a {x}"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses target-prefix return-helper stores as in-X value proofs");
    check_ops(result.ops,
              {jump("main"), label("save"), store("6"), ret(), label("main"), recall("4"),
               cjump("target"), jump("end"), label("target"), call("save"),
               plain(0x35, "\u041a {x}"), label("end"), halt()},
              "target-prefix return-helper store proof");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("save"),
                                       store("6"),
                                       ret(),
                                       label("outer"),
                                       call("save"),
                                       ret(),
                                       label("main"),
                                       recall("4"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       call("outer"),
                                       recall("6"),
                                       plain(0x35, "\u041a {x}"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses nested return-helper stores as target-prefix proofs");
    check_ops(result.ops,
              {jump("main"), label("save"), store("6"), ret(), label("outer"), call("save"), ret(),
               label("main"), recall("4"), cjump("target"), jump("end"), label("target"),
               call("outer"), plain(0x35, "\u041a {x}"), label("end"), halt()},
              "nested return-helper store proof");
  }

  {
    const std::vector<IrOp> program = {recall("6"),    numeric_cjump(4),    halt(),
                                       recall("6"),    plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "handles numeric conditional targets");
    check_ops(result.ops,
              {recall("6"), numeric_cjump(4), halt(), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
               halt()},
              "numeric conditional target drop");
  }

  {
    const std::vector<IrOp> program = {recall("6"),
                                       numeric_cjump(4),
                                       halt(),
                                       recall("6"),
                                       plain(0x54, "\u041a \u041d\u041e\u041f"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses projected X2 register state at numeric targets");
    check_ops(result.ops,
              {recall("6"), numeric_cjump(4), halt(), plain(0x54, "\u041a \u041d\u041e\u041f"),
               plain(0x0c, "\u0412\u041f"), halt()},
              "numeric target projected X2 drop");
  }

  {
    const std::vector<IrOp> program = {recall("6"),     numeric_cjump(4),   halt(),
                                       recall("6"),     numeric_jump(7),    plain(0x20, "F pi"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps numeric target recalls before later numeric targets");
    check_ops(result.ops, program, "numeric target before later numeric preserved");
  }

  {
    const std::vector<IrOp> program = {recall("6"),     cjump("target"),     jump("alias"),
                                       label("target"), label("alias"),      recall("6"),
                                       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps target recall when alias label is another entry");
    check_ops(result.ops, program, "alias label entry preserved");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       ret(),
                                       label("main"),
                                       plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       store("4"),
                                       recall("4"),
                                       cjump("target"),
                                       halt(),
                                       label("target"),
                                       recall("4"),
                                       plain(0x0b, "/-/"),
                                       call("transparent"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops target recall before sign VP via transparent helper");
    check_ops(result.ops,
              {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"), ret(),
               label("main"), plain(0x01, "1"), plain(0x02, "2"), store("4"), recall("4"),
               cjump("target"), halt(), label("target"), plain(0x0b, "/-/"), call("transparent"),
               plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), halt()},
              "target recall before sign VP via transparent helper drop");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"),
                                       store("6"),
                                       recall("1", "preload const 2"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       recall("6"),
                                       plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses decimal value proof at unique targets");
    check_ops(result.ops,
              {plain(0x02, "2"), store("6"), recall("1", "preload const 2"), cjump("target"),
               jump("end"), label("target"), plain(0x32, "\u041a \u0417\u041d"), label("end"),
               halt()},
              "decimal value proof at target");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),
                                       plain(0xf0, "F* empty F0"),
                                       store("6"),
                                       recall("2", "preload const FACE"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),
                                       plain(0xf0, "F* empty F0"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       recall("6"),
                                       plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses structural shape proof at unique targets");
    check_ops(result.ops,
              {recall("1", "preload const FACE"), plain(0x0c, "\u0412\u041f"), plain(0x03, "3"),
               plain(0xf0, "F* empty F0"), store("6"), recall("2", "preload const FACE"),
               plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), plain(0xf0, "F* empty F0"),
               cjump("target"), jump("end"), label("target"), plain(0x32, "\u041a \u0417\u041d"),
               label("end"), halt()},
              "structural shape proof at target");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const \u0413"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x0b, "/-/"),
                                       plain(0x02, "2"),
                                       store("6"),
                                       recall("2", "preload const \u0413E-2"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       recall("6"),
                                       plain(0x32, "\u041a \u0417\u041d"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses structural exponent preload proof at unique targets");
    check_ops(result.ops,
              {recall("1", "preload const \u0413"), plain(0x0c, "\u0412\u041f"), plain(0x0b, "/-/"),
               plain(0x02, "2"), store("6"), recall("2", "preload const \u0413E-2"),
               cjump("target"), jump("end"), label("target"), plain(0x32, "\u041a \u0417\u041d"),
               label("end"), halt()},
              "structural exponent preload proof at target");
  }

  {
    const std::vector<IrOp> program = {recall("4"),     cjump("target"), jump("end"),
                                       label("target"), store("5"),      recall("4"),
                                       plain(0x35, "\u041a {x}"), label("end"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "crosses X-preserving store prefixes at unique targets");
    check_ops(result.ops,
              {recall("4"), cjump("target"), jump("end"), label("target"), store("5"),
               plain(0x35, "\u041a {x}"), label("end"), halt()},
              "X-preserving store prefix target drop");
  }

  {
    const std::vector<IrOp> program = {recall("4"),     cjump("target"), jump("end"),
                                       label("target"), store("6"),      recall("6"),
                                       plain(0x35, "\u041a {x}"), label("end"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses target-prefix stores as in-X value proofs");
    check_ops(result.ops,
              {recall("4"), cjump("target"), jump("end"), label("target"), store("6"),
               plain(0x35, "\u041a {x}"), label("end"), halt()},
              "target-prefix store proof");
  }

  {
    const std::vector<IrOp> program = {recall("4"),
                                       cjump("target"),
                                       jump("end"),
                                       label("target"),
                                       known_target_indirect_store("8", "5"),
                                       recall("4"),
                                       plain(0x35, "\u041a {x}"),
                                       label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "crosses proved indirect store prefixes at unique targets");
    check_ops(result.ops,
              {recall("4"), cjump("target"), jump("end"), label("target"),
               known_target_indirect_store("8", "5"), plain(0x35, "\u041a {x}"), label("end"),
               halt()},
              "proved indirect store prefix target drop");
  }

  {
    const std::vector<IrOp> program = {recall("6"),    cjump("target"), jump("end"),
                                       label("target"), recall("6"),     plain(0x10, "+"),
                                       label("end"),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall that lifts stack for target binary op");
    check_ops(result.ops, program, "target stack-lift recall preserved");
  }

  {
    const std::vector<IrOp> program = {recall("6"),     plain(0x0e, "\u0412\u2191"), cjump("target"),
                                       jump("end"),     label("target"),             recall("6"),
                                       plain(0x10, "+"), label("end"),               halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops target recall before binary op with duplicate Y");
    check_ops(result.ops,
              {recall("6"), plain(0x0e, "\u0412\u2191"), cjump("target"), jump("end"),
               label("target"), plain(0x10, "+"), label("end"), halt()},
              "target recall before binary op with duplicate Y drop");
  }

  {
    const std::vector<IrOp> program = {recall("6"),     cjump("first"),  jump("end"),
                                       label("first"),  recall("6"),     cjump("second"),
                                       jump("end"),     label("second"), recall("6"),
                                       plain(0x10, "+"), label("end"),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "does not use target recall producer already removed");
    check_ops(result.ops,
              {recall("6"), cjump("first"), jump("end"), label("first"), cjump("second"),
               jump("end"), label("second"), recall("6"), plain(0x10, "+"), label("end"), halt()},
              "removed target producer not reused");
  }

  {
    const std::vector<IrOp> program = {recall("6"),     cjump("target"), jump("end"),
                                       label("target"), recall("6"),     plain(0x35, "\u041a {x}"),
                                       plain(0x10, "+"), label("end"),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps target recall whose stack lift reaches later binary op");
    check_ops(result.ops, program, "target stack-lift reaching binary op preserved");
  }

  {
    const std::vector<IrOp> program = {recall("6"), cjump("target"), plain(0x01, "1"),
                                       label("target"), recall("6"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall when target has fallthrough predecessor");
    check_ops(result.ops, program, "target with fallthrough predecessor preserved");
  }

  {
    IrOp selector_branch = known_target_indirect_cjump("8", 4);
    selector_branch.opcode = 0xe8;
    selector_branch.condition = "==0";
    selector_branch.meta.indirect_flow_targets = std::vector<IrTarget>{4};
    IrOp selector_store = known_target_indirect_store("8", "4");
    selector_store.meta.indirect_memory_targets = std::vector<int>{4};
    const std::vector<std::vector<IrOp>> programs{
        {recall("8"), selector_branch, jump("end"), label("target"),
         recall("8"), plain(0x35, "frac"), label("end"), halt()},
        {recall("8"), cjump("target"), jump("end"), label("target"),
         selector_store, recall("8"), plain(0x35, "frac"), label("end"), halt()},
        {jump("main"), label("selector_helper"), selector_store, ret(),
         label("main"), recall("8"), cjump("target"), jump("end"),
         label("target"), call("selector_helper"), recall("8"), plain(0x35, "frac"),
         label("end"), halt()},
    };
    const auto visible_result = [](const std::vector<IrOp>& program,
                                   const std::string& selector = "4.375") {
      const auto resolved = resolve_machine_items(lower_ir_to_machine(program), {});
      require(resolved.diagnostics.empty(), "selector writeback fixture must resolve");
      std::vector<int> codes;
      for (const auto& step : resolved.steps)
        codes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(),
              "selector writeback fixture must load");
      calc.set_register("8", selector);
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(1000, 6).stopped,
              "selector writeback fixture must stop");
      return calc.read_register("x");
    };
    for (const auto& program : programs) {
      const auto result = run(program);
      check_applied(result.applied, 0,
                    "stable selector writeback invalidates branch and helper-prefix aliases");
      check_ops(result.ops, program, "selector writeback target recall must remain");

      auto unsafe = program;
      for (auto it = unsafe.end(); it != unsafe.begin();) {
        --it;
        if (it->kind == IrKind::Recall && it->register_name == "8") {
          unsafe.erase(it);
          break;
        }
      }
      require(visible_result(program) == visible_result(result.ops),
              "the pass must preserve the post-writeback fractional result");
      require(visible_result(program) != visible_result(unsafe),
              "ROM must expose the old invalid deletion: zero versus 0.375");
    }

    const std::vector<IrOp> straight{
        recall("8"), selector_store, recall("8"), plain(0x35, "frac"), halt()};
    const auto straight_result = core::passes::flow_x_reuse(straight, ctx);
    check_applied(straight_result.applied, 0,
                  "CFG X aliases must also invalidate a stable indirect-store selector");
    require(visible_result(straight) == visible_result(straight_result.ops),
            "the generic X-reuse pass must preserve selector writeback");

    IrOp indirect_read = known_target_indirect_recall("8", "6");
    indirect_read.meta.indirect_memory_targets = std::vector<int>{6};
    const std::vector<IrOp> read_then_observe{
        recall("6"), indirect_read, recall("8"), plain(0x35, "frac"), halt()};
    const auto read_result = core::passes::flow_x_reuse(read_then_observe, ctx);
    check(count_indirect_recall(read_result.ops) == 1,
          "removing a redundant indirect load must not erase an observed selector writeback");
    require(visible_result(read_then_observe, "6.375") ==
                visible_result(read_result.ops, "6.375"),
            "indirect recall deletion must preserve later selector data");
    const std::vector<IrOp> read_then_call{
        recall("6"), indirect_read, call("observe_selector"), halt(),
        label("observe_selector"), recall("8"), plain(0x35, "frac"), ret()};
    const auto called_read_result = core::passes::flow_x_reuse(read_then_call, ctx);
    check(count_indirect_recall(called_read_result.ops) == 1,
          "selector-writeback liveness must follow a called observer");

    for (const auto& safe_read : {
             std::vector<IrOp>{recall("6"), indirect_read, plain(0x35, "frac"), halt()},
             std::vector<IrOp>{recall("6"), indirect_read, store("8"),
                              recall("8"), plain(0x35, "frac"), halt()}}) {
      const auto safe_result = core::passes::flow_x_reuse(safe_read, ctx);
      check(count_indirect_recall(safe_result.ops) == 0,
            "unobserved or overwritten selector writeback must not block redundant-read removal");
      require(visible_result(safe_read, "6.375") ==
                  visible_result(safe_result.ops, "6.375"),
              "proved-dead selector writeback removal must match ROM behavior");
    }

    IrOp self_store = known_target_indirect_store("8", "8");
    self_store.meta.indirect_memory_targets = std::vector<int>{8};
    const std::vector<IrOp> restored{
        recall("8"), cjump("target"), jump("end"), label("target"),
        self_store, recall("8"), plain(0x35, "frac"), label("end"), halt()};
    const auto restored_result = run(restored);
    check_applied(restored_result.applied, 1,
                  "a known self-store re-establishes the alias after selector writeback");
    require(visible_result(restored, "8.375") ==
                visible_result(restored_result.ops, "8.375"),
            "selector self-store order must match the ROM");

    auto initial = core::passes::empty_x2_value_dataflow_state(true);
    initial.x = {"reg:8"};
    initial.y = core::passes::X2ValueSet{"reg:4"};
    initial.x2 = {"reg:4"};
    const auto unknown = core::passes::transfer_x2_value_state_for_edge(
        initial, indirect_store("8"), core::passes::X2DataflowEdgeKind::Normal,
        {.track_register_memory = true});
    require(unknown.has_value() && !unknown->x.contains("reg:8") &&
                (!unknown->y.has_value() || !unknown->y->contains("reg:4")) &&
                !unknown->x2.contains("reg:4"),
            "an unknown indirect store must invalidate dependencies, not only memory caches");
  }


  {
    // Distinguish the selector bank cell from the loaded/stored destination
    // across decrementing, incrementing and stable addressing families.
    const auto observe = [](const std::vector<IrOp>& program,
                            const std::string& selector, const std::string& seed) {
      const auto resolved = resolve_machine_items(lower_ir_to_machine(program), {});
      require(resolved.diagnostics.empty(), "selector alias matrix must resolve");
      std::vector<int> codes;
      for (const auto& step : resolved.steps)
        codes.push_back(step.opcode);
      emulator::MK61 calc;
      require(calc.load_program(codes).diagnostics.empty(),
              "selector alias matrix must load");
      const std::string registers = "0123456789abcde";
      for (int reg = 0; reg < 15; ++reg)
        calc.set_register(registers.substr(static_cast<std::size_t>(reg), 1),
                          std::to_string(reg) + ".125");
      calc.set_register(selector, seed);
      calc.press_sequence({"В/О", "С/П"});
      require(calc.run_until_stable(1000, 6).stopped,
              "selector alias matrix must stop");
      return calc.read_register("x");
    };
    int cases = 0;
    int optimized_cases = 0;
    for (const char selector_char : std::string("024678e")) {
      const std::string selector(1, selector_char);
      for (const std::string seed : {"2.375", "4.375", "8.375", "10.375"}) {
        const auto evaluated = core::evaluate_indirect_address(
            selector, seed, core::IndirectOperationKind::Memory);
        require(evaluated.has_value() && evaluated->memory_target.has_value(),
                "selector alias matrix needs a complete memory target");
        const int target_index = *evaluated->memory_target;
        const std::string target =
            std::string("0123456789abcde").substr(static_cast<std::size_t>(target_index), 1);
        for (const bool write : {false, true}) {
          for (const std::string& initial : {selector, target}) {
            for (const std::string& final : {selector, target}) {
              for (const int consumer : {0x35, 0x0f, 0x0c}) {
                IrOp access = write ? known_target_indirect_store(selector, target)
                                    : known_target_indirect_recall(selector, target);
                access.meta.indirect_memory_targets = std::vector<int>{target_index};
                std::vector<IrOp> program{
                    recall(initial), access, recall(final), plain(consumer, "consumer")};
                if (consumer == 0x0c)
                  program.push_back(plain(2, "2"));
                program.push_back(halt());
                const auto result = core::passes::flow_x_reuse(program, ctx);
                ++cases;
                optimized_cases += result.applied != 0 ? 1 : 0;
                require(observe(program, selector, seed) ==
                            observe(result.ops, selector, seed),
                        "selector alias ROM mismatch: selector=" + selector +
                            " seed=" + seed + " target=" + target +
                            " write=" + std::to_string(write) +
                            " initial=" + initial + " final=" + final +
                            " consumer=" + std::to_string(consumer));
              }
            }
          }
        }
      }
    }
    require(cases == 672 && optimized_cases > 0,
            "selector alias coverage must include all families and real rewrites");
  }


  // Full parity: the structural shape-proof and structural exponent-preload
  // proofs at unique branch targets now match the TS oracle exactly.
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
  require(message.empty(), "branch-target-x-reuse parity divergence set changed:" + message);
}

} // namespace mkpro::tests
