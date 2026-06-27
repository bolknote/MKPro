#include "mkpro/core/passes/store_recall_peephole.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (store-recall-peephole describe block, 29 cases)
//
// Cases that depend on the not-yet-ported X2 decimal-digit-run / structural
// VP-source recall-removal machinery are tracked in kDeferred and guarded by an
// exact divergence-set check (see last_x_reuse_test.cpp for rationale).

void store_recall_peephole_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::store_recall_peephole_pass().run(program, ctx);
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
  const auto check_not_contains = [&](const std::vector<IrOp>& a, const IrOp& needle,
                                      const std::string& label) {
    const std::string needle_json = mkpro::ir_ops_to_json({needle});
    for (const IrOp& op : a) {
      if (mkpro::ir_ops_to_json({op}) == needle_json) {
        failures.push_back(label);
        return;
      }
    }
  };

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall immediately after store same register");
    if (result.ops.size() != 2U)
      failures.push_back("drops recall immediately after store same register length");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_store("8", "2"),
                                       known_target_indirect_recall("8", "2"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops stable indirect recall after same-target store");
    check_ops(result.ops, {known_target_indirect_store("8", "2"), halt()},
              "stable indirect store/recall drop");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"), store("6"),
                                       recall("1", "preload const 2"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall when value proof shows X unchanged");
    check_ops(result.ops, {plain(0x02, "2"), store("6"), halt()}, "value-proof recall drop");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"), store("6"),
                                       recall("2", "preload const FACE"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops structural recall when shape proof shows X unchanged");
    check_ops(result.ops, {recall("1", "preload const FACE"), store("6"), halt()},
              "shape-proof recall drop");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("6"),
                                       recall("2", "preload const FACE"),
                                       orphan_address(54),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops structural recall before VP across address gaps");
    check_ops(result.ops,
              {recall("1", "preload const FACE"), store("6"), orphan_address(54),
               plain(0x0c, "\u0412\u041f"), halt()},
              "structural recall before VP across gaps");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const \u0413"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x0b, "/-/"),
                                       plain(0x02, "2"),
                                       store("6"),
                                       recall("2", "preload const \u0413E-2"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops structural exponent preload recall on shape proof");
    check_ops(result.ops,
              {recall("1", "preload const \u0413"), plain(0x0c, "\u0412\u041f"), plain(0x0b, "/-/"),
               plain(0x02, "2"), store("6"), halt()},
              "structural exponent preload recall drop");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_store("4", "2"),
                                       known_target_indirect_recall("4", "2"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps mutating indirect store/recall pairs");
    check_ops(result.ops, program, "mutating indirect pair preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall that syncs X2 before VP");
    check_ops(result.ops, program, "recall before VP preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), plain(0x0b, "/-/"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall that syncs X2 before sign-change");
    check_ops(result.ops, program, "recall before sign-change preserved");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),  plain(0x02, "2"),
                                       store("2"),        recall("2"),
                                       plain(0x0b, "/-/"), plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),  halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before sign VP when sign source matches");
    check_ops(result.ops,
              {plain(0x01, "1"), plain(0x02, "2"), store("2"), plain(0x0b, "/-/"),
               plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), halt()},
              "decimal recall before sign VP drop");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("2"),
                                       recall("2"),
                                       plain(0x0b, "/-/"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops structural recall before sign VP when source matches");
    check_ops(result.ops,
              {recall("1", "preload const FACE"), store("2"), plain(0x0b, "/-/"),
               plain(0x0c, "\u0412\u041f"), halt()},
              "structural recall before sign VP drop");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       ret(),
                                       label("main"),
                                       plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       store("2"),
                                       recall("2"),
                                       plain(0x0b, "/-/"),
                                       call("transparent"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before sign VP via transparent helper");
    check_ops(result.ops,
              {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"), ret(),
               label("main"), plain(0x01, "1"), plain(0x02, "2"), store("2"), plain(0x0b, "/-/"),
               call("transparent"), plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), halt()},
              "recall before sign VP via transparent helper drop");
  }

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall through X2-preserving ops before VP");
    check_ops(result.ops, program, "recall through preserving ops before VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("2"),       store("2"),
                                       recall("2"),       plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops redundant X2 sync through preserving ops before VP");
    check_ops(result.ops,
              {recall("2"), store("2"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()},
              "redundant X2 sync through preserving ops drop");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),  plain(0x02, "2"),
                                       store("2"),        recall("2"),
                                       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses value X2 aliases from literal stores before VP gaps");
    check_ops(result.ops,
              {plain(0x01, "1"), plain(0x02, "2"), store("2"), plain(0x20, "F pi"),
               plain(0x0c, "\u0412\u041f"), halt()},
              "literal-store X2 alias reuse");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"),       store("2"),
                                       recall("2"),      plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps value-backed X2 sync before immediate VP context");
    check_ops(result.ops, program, "value-backed X2 sync before VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("2"), store("2"), recall("2"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps redundant X2 sync before immediate VP context");
    check_ops(result.ops, program, "redundant X2 sync before VP preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"),  recall("2"), cjump("skip"),
                                       plain(0x0c, "\u0412\u041f"), halt(),
                                       label("skip"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before fallthrough VP after conditional X2 sync");
    check_not_contains(result.ops, recall("2"), "conditional fallthrough recall removed");
  }

  {
    const std::vector<IrOp> program = {store("2"),  recall("2"), loop("skip"),
                                       plain(0x0c, "\u0412\u041f"), halt(),
                                       label("skip"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before fallthrough VP after counted-loop X2 sync");
    check_not_contains(result.ops, recall("2"), "counted-loop fallthrough recall removed");
  }

  {
    const std::vector<IrOp> program = {store("0"),  recall("0"), loop("restore"),
                                       halt(),      label("restore"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps loop-counter recalls before counted-loop target VP");
    check_ops(result.ops, program, "loop-counter recall preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"),  recall("2"), cjump("restore"),
                                       halt(),      label("restore"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall before jump-target VP after conditional");
    check_ops(result.ops, program, "recall before jump-target VP preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall that lifts stack for immediate binary op");
    check_ops(result.ops, program, "stack-lift recall before binary op preserved");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), store("2"), plain(0x0e, "\u0412\u2191"),
                                       store("2"), recall("2"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before binary op when Y already duplicated");
    check_ops(result.ops,
              {plain(0x01, "1"), store("2"), plain(0x0e, "\u0412\u2191"), store("2"),
               plain(0x10, "+"), halt()},
              "recall before binary op with duplicate Y removed");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), store("1"), recall("1"), store("2"),
                                       recall("2"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "does not use recall producer already removed in same pass");
    check_ops(result.ops,
              {plain(0x01, "1"), store("1"), store("2"), recall("2"), plain(0x10, "+"), halt()},
              "removed producer not reused");
  }

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), plain(0x35, "\u041a {x}"),
                                       plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall whose stack lift reaches later binary op");
    check_ops(result.ops, program, "stack-lift recall reaching binary op preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"),  recall("2"),
                                       call("frac"), plain(0x10, "+"),
                                       halt(),      label("frac"),
                                       plain(0x35, "\u041a {x}"), ret()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall whose lift reaches binary op after direct call");
    check_ops(result.ops, program, "stack-lift recall after direct call preserved");
  }

  {
    const std::vector<IrOp> program = {store("2"),  recall("2"),    call("noop"),
                                       plain(0x0c, "\u0412\u041f"), halt(),
                                       label("noop"), ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before direct call return that syncs X2 before VP");
    check_ops(result.ops,
              {store("2"), call("noop"), plain(0x0c, "\u0412\u041f"), halt(), label("noop"), ret()},
              "direct call return X2 sync recall removed");
  }

  {
    const std::vector<IrOp> program = {store("2"),
                                       recall("2"),
                                       known_target_indirect_call("7", 5),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt(),
                                       label("noop"),
                                       ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before known indirect call return syncing X2");
    check_ops(result.ops,
              {store("2"), known_target_indirect_call("7", 5), plain(0x0c, "\u0412\u041f"), halt(),
               label("noop"), ret()},
              "indirect call return X2 sync recall removed");
  }

  {
    const std::vector<IrOp> program = {store("2"), recall("2"), plain(0x01, "1"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "still drops recall before a fresh digit entry");
  }

  // Full parity: the structural shape-proof and structural exponent-preload
  // recall-removal cases now match the TS oracle exactly.
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
  require(message.empty(), "store-recall-peephole parity divergence set changed:" + message);
}

} // namespace mkpro::tests
