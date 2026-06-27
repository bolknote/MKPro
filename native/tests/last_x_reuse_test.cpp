#include "mkpro/core/passes/last_x_reuse.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (last-x-reuse describe block, 51 cases)
//
// All 51 cases match the native pass exactly, including the structural
// hex-shape and structural exponent-preload recall-removal proofs that consult
// the X2 value-dataflow shape facts. The deferred set below is empty; the exact
// divergence-set check still guards against any future regression and forces a
// promotion if the divergence set ever changes.

void last_x_reuse_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::last_x_reuse_pass().run(program, ctx);
  };
  std::vector<std::string> failures;
  const auto require_applied = [&](int actual, int expected, const std::string& label) {
    if (actual != expected)
      failures.push_back(label);
  };
  const auto require_ops_equal = [&](const std::vector<IrOp>& a, const std::vector<IrOp>& e,
                                     const std::string& label) {
    if (mkpro::ir_ops_to_json(a) != mkpro::ir_ops_to_json(e))
      failures.push_back(label);
  };

  {
    const std::vector<IrOp> program = {store("1"), recall("1")};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops recall when X already holds value");
    require(result.ops.size() == 1U, "drops recall: result length 1");
    require(result.ops.at(0).kind == IrKind::Store, "drops recall: remaining op is store");
  }

  {
    const std::vector<IrOp> program = {store("5"), known_target_indirect_recall("7", "5"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops stable indirect recall with proved target");
    require_ops_equal(result.ops, {store("5"), halt()}, "stable indirect recall proved target");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_store("8", "5"), recall("5"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "tracks X through stable indirect store");
    require_ops_equal(result.ops, {known_target_indirect_store("8", "5"), halt()},
                      "stable indirect store tracks X");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_store("4", "5"), recall("5"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "tracks X through mutating indirect store");
    require_ops_equal(result.ops, {known_target_indirect_store("4", "5"), halt()},
                      "mutating indirect store keeps side effect");
  }

  {
    const std::vector<IrOp> program = {known_target_indirect_store("8", "5"),
                                       known_target_indirect_recall("4", "5"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps mutating indirect recall after indirect store");
    require_ops_equal(result.ops, program, "mutating indirect recall preserved");
  }

  {
    const std::vector<IrOp> program = {store("5"), known_target_indirect_recall("1", "5"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps predecrement indirect recall");
    require_ops_equal(result.ops, program, "predecrement indirect recall preserved");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps recall syncing X2 before VP");
    require_ops_equal(result.ops, program, "recall before VP preserved");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), plain(0x0b, "/-/"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps recall syncing X2 before sign-change");
    require_ops_equal(result.ops, program, "recall before sign-change preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1"), store("1"), recall("1"), plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops redundant X2 sync when X2 holds same register");
    require_ops_equal(result.ops,
                      {recall("1"), store("1"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                       halt()},
                      "redundant X2 sync removed");
  }

  {
    const std::vector<IrOp> program = {recall("1"), cjump("done"),  recall("1"),
                                       halt(),      label("done"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "keeps X through direct conditional fallthrough");
    require_ops_equal(result.ops, {recall("1"), cjump("done"), halt(), label("done"), halt()},
                      "direct conditional fallthrough reuse");
  }

  {
    const std::vector<IrOp> program = {recall("4"), loop("done"),  recall("4"),
                                       halt(),      label("done"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "keeps non-counter X through counted loop fallthrough");
    require_ops_equal(result.ops, {recall("4"), loop("done"), halt(), label("done"), halt()},
                      "counted loop fallthrough reuse");
  }

  {
    const std::vector<IrOp> program = {recall("0"), loop("done"),  recall("0"),
                                       halt(),      label("done"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "drops counted-loop counter alias on fallthrough");
    require_ops_equal(result.ops, program, "counted loop counter alias preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1"),
                                       store("1"),
                                       recall("1"),
                                       known_target_indirect_jump("8", 5),
                                       halt(),
                                       label("target"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops redundant X2 sync before VP via proved indirect flow");
    require_ops_equal(result.ops,
                      {recall("1"), store("1"), known_target_indirect_jump("8", 5), halt(),
                       label("target"), plain(0x0c, "\u0412\u041f"), halt()},
                      "redundant X2 sync via indirect flow removed");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),     plain(0x02, "2"),
                                       store("2"),           plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       recall("2"),          plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "uses value X2 aliases from literal stores before VP gaps");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), plain(0x02, "2"), store("2"),
                       plain(0x54, "\u041a\u041d\u041e\u041f"), plain(0x20, "F pi"),
                       plain(0x0c, "\u0412\u041f"), halt()},
                      "literal-store X2 alias reuse");
  }

  {
    const std::vector<IrOp> program = {
        plain(0x01, "1"),  plain(0x02, "2"), store("2"),
        plain(0x0d, "Cx"), plain(0x01, "1"), plain(0x02, "2"),
        recall("2"),       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "uses decimal register memory when X rebuilt as literal");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), plain(0x02, "2"), store("2"), plain(0x0d, "Cx"),
                       plain(0x01, "1"), plain(0x02, "2"), plain(0x20, "F pi"),
                       plain(0x0c, "\u0412\u041f"), halt()},
                      "decimal register memory reuse");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"),
                                       recall("2", "preload const 12"), plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops preloaded decimal recall when X rebuilt as literal");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), plain(0x02, "2"), plain(0x20, "F pi"),
                       plain(0x0c, "\u0412\u041f"), halt()},
                      "preloaded decimal recall removed");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("2"),
                                       plain(0x0d, "Cx"),
                                       recall("3", "preload const FACE"),
                                       recall("2"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops structural recall when X rebuilt as same hex shape");
    require_ops_equal(result.ops,
                      {recall("1", "preload const FACE"), store("2"), plain(0x0d, "Cx"),
                       recall("3", "preload const FACE"), halt()},
                      "structural hex shape recall removed");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const \u0413"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x0b, "/-/"),
                                       plain(0x02, "2"),
                                       recall("2", "preload const \u0413E-2"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops structural exponent preload recall");
    require_ops_equal(result.ops,
                      {recall("1", "preload const \u0413"), plain(0x0c, "\u0412\u041f"),
                       plain(0x0b, "/-/"), plain(0x02, "2"), halt()},
                      "structural exponent preload recall removed");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("2"),
                                       plain(0x0d, "Cx"),
                                       recall("3", "preload const FACE"),
                                       recall("2"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops structural recall before immediate VP match");
    require_ops_equal(result.ops,
                      {recall("1", "preload const FACE"), store("2"), plain(0x0d, "Cx"),
                       recall("3", "preload const FACE"), plain(0x0c, "\u0412\u041f"), halt()},
                      "structural recall before VP removed");
  }

  {
    const std::vector<IrOp> program = {
        plain(0x01, "1"),  plain(0x02, "2"), store("2"),
        plain(0x0d, "Cx"), plain(0x01, "1"), plain(0x02, "2"),
        recall("2"),       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops decimal recall before immediate VP match");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), plain(0x02, "2"), store("2"), plain(0x0d, "Cx"),
                       plain(0x01, "1"), plain(0x02, "2"), plain(0x0c, "\u0412\u041f"), halt()},
                      "decimal recall before VP removed");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),  plain(0x02, "2"),
                                       store("2"),        recall("2"),
                                       plain(0x0b, "/-/"), plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),  halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops decimal recall before sign VP match");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), plain(0x02, "2"), store("2"), plain(0x0b, "/-/"),
                       plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), halt()},
                      "decimal recall before sign VP removed");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("2"),
                                       recall("2"),
                                       plain(0x0b, "/-/"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops structural recall before sign VP match");
    require_ops_equal(result.ops,
                      {recall("1", "preload const FACE"), store("2"), plain(0x0b, "/-/"),
                       plain(0x0c, "\u0412\u041f"), halt()},
                      "structural recall before sign VP removed");
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
    require_applied(result.applied, 1, "drops decimal recall before sign VP via transparent helper");
    require_ops_equal(result.ops,
                      {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"),
                       ret(), label("main"), plain(0x01, "1"), plain(0x02, "2"), store("2"),
                       plain(0x0b, "/-/"), call("transparent"), plain(0x0c, "\u0412\u041f"),
                       plain(0x03, "3"), halt()},
                      "decimal recall via transparent helper removed");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       known_target_indirect_store("8", "2"),
                                       plain(0x0d, "Cx"),
                                       plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       known_target_indirect_recall("8", "2"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops stable indirect decimal recall before VP match");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), plain(0x02, "2"), known_target_indirect_store("8", "2"),
                       plain(0x0d, "Cx"), plain(0x01, "1"), plain(0x02, "2"),
                       plain(0x0c, "\u0412\u041f"), halt()},
                      "stable indirect decimal recall removed");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), store("2"),
                                       plain(0x0d, "Cx"), plain(0x01, "1"), plain(0x02, "2"),
                                       store("4"), recall("2"), plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps decimal recall when store reset VP source");
    require_ops_equal(result.ops, program, "decimal recall with reset VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("2"),
                                       plain(0x0d, "Cx"),
                                       recall("3", "preload const FACE"),
                                       store("4"),
                                       recall("2"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps structural recall when store reset VP source");
    require_ops_equal(result.ops, program, "structural recall with reset VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("2"),
                                       plain(0x0d, "Cx"),
                                       recall("3", "preload const FACE"),
                                       recall("2"),
                                       plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops structural recall before preserving gap and VP");
    require_ops_equal(result.ops,
                      {recall("1", "preload const FACE"), store("2"), plain(0x0d, "Cx"),
                       recall("3", "preload const FACE"), plain(0x20, "F pi"),
                       plain(0x0c, "\u0412\u041f"), halt()},
                      "structural recall before preserving gap removed");
  }

  {
    const std::vector<IrOp> program = {recall("1"), store("1"), recall("1"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps redundant X2 sync before immediate VP context");
    require_ops_equal(result.ops, program, "redundant X2 sync before VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1"), store("2"), recall("2"), plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops recall when X2 holds alias from same X value");
    require_ops_equal(result.ops,
                      {recall("1"), store("2"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                       halt()},
                      "X2 alias recall removed");
  }

  {
    const std::vector<IrOp> program = {store("1"),  recall("1"),    call("noop"),
                                       plain(0x0c, "\u0412\u041f"), halt(),
                                       label("noop"), ret()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops recall when direct return syncs X2 before VP");
    require_ops_equal(result.ops,
                      {store("1"), call("noop"), plain(0x0c, "\u0412\u041f"), halt(), label("noop"),
                       ret()},
                      "direct return X2 sync recall removed");
  }

  {
    const std::vector<IrOp> program = {store("1"),
                                       recall("1"),
                                       known_target_indirect_call("7", 5),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt(),
                                       label("noop"),
                                       ret()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops recall when known indirect return syncs X2 before VP");
    require_ops_equal(result.ops,
                      {store("1"), known_target_indirect_call("7", 5), plain(0x0c, "\u0412\u041f"),
                       halt(), label("noop"), ret()},
                      "indirect return X2 sync recall removed");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       ret(),
                                       label("main"),
                                       recall("4"),
                                       call("transparent"),
                                       recall("4"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "preserves X facts through transparent return helpers");
    require_ops_equal(result.ops,
                      {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"),
                       ret(), label("main"), recall("4"), call("transparent"), halt()},
                      "transparent return helper X reuse");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       ret(),
                                       label("main"),
                                       recall("4"),
                                       known_target_indirect_call("8", 2),
                                       recall("4"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "preserves X facts through proved stable-indirect helpers");
    require_ops_equal(result.ops,
                      {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"),
                       ret(), label("main"), recall("4"), known_target_indirect_call("8", 2),
                       halt()},
                      "stable-indirect transparent helper X reuse");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "\u041a\u041d\u041e\u041f"),
                                       ret(),
                                       label("main"),
                                       recall("4"),
                                       known_target_indirect_call("4", 2),
                                       recall("4"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "drops selector X facts across mutating indirect helpers");
    require_ops_equal(result.ops, program, "mutating indirect helper preserves recall");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("clobber"),
                                       plain(0x0d, "Cx"),
                                       ret(),
                                       label("main"),
                                       recall("4"),
                                       call("clobber"),
                                       recall("4"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "treats nontransparent return helpers as X barriers");
    require_ops_equal(result.ops, program, "nontransparent helper preserves recall");
  }

  {
    const std::vector<IrOp> program = {recall("1"),
                                       plain(0x54, "\u041a \u041d\u041e\u041f"),
                                       plain(0x55, "\u041a 1"),
                                       plain(0x56, "\u041a 2"),
                                       recall("1"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "preserves X facts through documented empty operators");
    require_ops_equal(result.ops,
                      {recall("1"), plain(0x54, "\u041a \u041d\u041e\u041f"), plain(0x55, "\u041a 1"),
                       plain(0x56, "\u041a 2"), halt()},
                      "documented empty operators X reuse");
  }

  {
    const std::vector<IrOp> program = {recall("1"), label("marker"),
                                       plain(0x54, "\u041a \u041d\u041e\u041f"), recall("1"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "preserves X facts through unreferenced labels");
    require_ops_equal(result.ops,
                      {recall("1"), label("marker"), plain(0x54, "\u041a \u041d\u041e\u041f"),
                       halt()},
                      "unreferenced label X reuse");
  }

  {
    const std::vector<IrOp> program = {recall("1"), jump("entry"), label("entry"), recall("1"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "clears X facts at referenced labels");
    require_ops_equal(result.ops, program, "referenced label clears X facts");
  }

  {
    const std::vector<IrOp> program = {recall("1"), proc_start("helper"), recall("1"),
                                       proc_end("helper"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "clears X facts at procedure starts");
    require_ops_equal(result.ops, program, "procedure start clears X facts");
  }

  {
    const std::vector<IrOp> program = {recall("1"), numeric_jump(3), label("entry"), recall("1"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "clears X facts at numeric-address labels");
    require_ops_equal(result.ops, program, "numeric-address label clears X facts");
  }

  {
    const std::vector<IrOp> program = {recall("1"), known_target_indirect_jump("8", 2),
                                       label("entry"), recall("1"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "clears X facts at proved indirect-flow labels");
    require_ops_equal(result.ops, program, "proved indirect-flow label clears X facts");
  }

  {
    const std::vector<IrOp> program = {recall("1"), indirect_jump("8"), label("entry"), recall("1"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "treats unknown indirect flow as label entry hazard");
    require_ops_equal(result.ops, program, "unknown indirect flow label hazard");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps recall that lifts stack for immediate binary op");
    require_ops_equal(result.ops, program, "stack-lift recall before binary op preserved");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), store("1"), plain(0x0e, "\u0412\u2191"),
                                       recall("1"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops recall before binary op when Y already duplicated");
    require_ops_equal(result.ops,
                      {plain(0x01, "1"), store("1"), plain(0x0e, "\u0412\u2191"), plain(0x10, "+"),
                       halt()},
                      "recall before binary op with duplicate Y removed");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), recall("1"), plain(0x10, "+"),
                                       halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "does not use recall producer already removed in same pass");
    require_ops_equal(result.ops, {store("1"), recall("1"), plain(0x10, "+"), halt()},
                      "removed producer not reused");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), plain(0x35, "\u041a {x}"),
                                       plain(0x10, "+"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps recall whose stack lift reaches later binary op");
    require_ops_equal(result.ops, program, "stack-lift recall reaching binary op preserved");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), known_target_indirect_jump("8", 3),
                                       label("target"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 1, "drops recall before proved indirect flow when lift dead");
    require_ops_equal(result.ops,
                      {store("1"), known_target_indirect_jump("8", 3), label("target"), halt()},
                      "dead stack-lift recall removed");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), known_target_indirect_jump("8", 3),
                                       label("target"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps recall whose lift reaches binary op via indirect flow");
    require_ops_equal(result.ops, program, "stack-lift recall via indirect flow preserved");
  }

  {
    const std::vector<IrOp> program = {store("1"),  recall("1"),
                                       call("frac"), plain(0x10, "+"),
                                       halt(),      label("frac"),
                                       plain(0x35, "\u041a {x}"), ret()};
    const auto result = run(program);
    require_applied(result.applied, 0, "keeps recall whose lift reaches binary op after direct call");
    require_ops_equal(result.ops, program, "stack-lift recall after direct call preserved");
  }

  {
    const std::vector<IrOp> program = {store("1"), halt(), recall("1")};
    const auto result = run(program);
    require_applied(result.applied, 0, "refuses to fire across a stop barrier");
  }

  {
    const std::vector<IrOp> program = {store("1"), plain(0x10, "+"), recall("1")};
    const auto result = run(program);
    require_applied(result.applied, 0, "refuses to fire across an ALU op that clobbers X");
  }

  // Full parity: every last-x-reuse case (including the structural hex-shape
  // and structural exponent-preload recall-removal proofs) now matches the TS
  // oracle exactly.
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
  require(message.empty(), "last-x-reuse parity divergence set changed:" + message);
}

} // namespace mkpro::tests
