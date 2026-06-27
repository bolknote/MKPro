#include "mkpro/core/passes/branch_target_x_reuse.hpp"

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
