#include "mkpro/core/passes/flow_x_reuse.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (flow-x-reuse describe block, 38 cases)
//
// Cases depending on the not-yet-ported X2 decimal-digit-run / structural
// VP-source machinery are tracked in kDeferred and guarded by an exact
// divergence-set check (see last_x_reuse_test.cpp for rationale).

void flow_x_reuse_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::flow_x_reuse_pass().run(program, ctx);
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
  const auto count_recall = [](const std::vector<IrOp>& a, const std::string& reg) {
    int n = 0;
    for (const IrOp& op : a)
      if (op.kind == IrKind::Recall && op.register_name == reg)
        ++n;
    return n;
  };
  const auto check = [&](bool ok, const std::string& label) {
    if (!ok)
      failures.push_back(label);
  };

  {
    const std::vector<IrOp> program = {recall("4"), jump("tail"), plain(0x00, "0"), label("tail"),
                                       recall("4"), store("5")};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall reached through direct jump");
    check(!result.optimizations.empty() && result.optimizations.at(0).name == "flow-x-reuse",
          "direct jump optimization name");
    check(count_recall(result.ops, "4") == 1, "direct jump recall count");
  }

  {
    const std::vector<IrOp> program = {recall("4"), known_target_indirect_jump("8", 3), halt(),
                                       label("tail"), recall("4"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "follows proved stable indirect flow targets");
    check(count_recall(result.ops, "4") == 1, "stable indirect flow recall count");
  }

  {
    const std::vector<IrOp> program = {recall("4"), indirect_jump("8"), halt(), label("tail"),
                                       recall("4"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "refuses unknown indirect flow targets");
    check_ops(result.ops, program, "unknown indirect flow preserved");
  }

  {
    const std::vector<IrOp> program = {recall("4"), known_target_indirect_jump("1", 3), halt(),
                                       label("tail"), recall("4"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "preserves unrelated X facts across mutating indirect flow");
    check(count_recall(result.ops, "4") == 1, "mutating indirect flow unrelated recall count");
  }

  {
    const std::vector<IrOp> program = {recall("1"), known_target_indirect_jump("1", 3), halt(),
                                       label("tail"), recall("1"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "clears mutated selector X fact across indirect flow");
    check_ops(result.ops, program, "mutated selector indirect flow preserved");
  }

  {
    const std::vector<IrOp> program = {recall("5"),
                                       jump("tail"),
                                       plain(0x00, "0"),
                                       label("tail"),
                                       known_target_indirect_recall("7", "5"),
                                       store("6")};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops stable indirect recall with proved target in X");
    bool has_indirect = false;
    for (const IrOp& op : result.ops)
      if (op.kind == IrKind::IndirectRecall)
        has_indirect = true;
    check(!has_indirect, "stable indirect recall removed");
  }

  {
    const std::vector<IrOp> program = {recall("5"),
                                       jump("tail"),
                                       plain(0x00, "0"),
                                       label("tail"),
                                       known_target_indirect_recall("4", "5"),
                                       store("6")};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps preincrement indirect recall");
    check_ops(result.ops, program, "preincrement indirect recall preserved");
  }

  {
    const std::vector<IrOp> program = {store("4"), recall("4"), plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall syncing X2 before preserving op and VP");
    check_ops(result.ops, program, "recall before preserving op and VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("4"),       store("5"),
                                       recall("4"),       plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops redundant X2 sync before preserving op and VP");
    check_ops(result.ops,
              {recall("4"), store("5"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()},
              "redundant X2 sync before preserving op drop");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"),       store("4"),
                                       jump("tail"),     plain(0x00, "0"),       label("tail"),
                                       recall("4"),      plain(0x20, "F pi"),    plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses value X2 aliases from literal stores before VP gaps");
    check_ops(result.ops,
              {plain(0x01, "1"), plain(0x02, "2"), store("4"), jump("tail"), plain(0x00, "0"),
               label("tail"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()},
              "literal-store X2 alias reuse");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), store("4"),
                                       jump("tail"),     plain(0x00, "0"), label("tail"),
                                       recall("4"),      plain(0x0b, "/-/"), plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses store-backed sign sources before sign VP gaps");
    check_ops(result.ops,
              {plain(0x01, "1"), plain(0x02, "2"), store("4"), jump("tail"), plain(0x00, "0"),
               label("tail"), plain(0x0b, "/-/"), plain(0x0c, "\u0412\u041f"), plain(0x03, "3"),
               halt()},
              "store-backed sign source reuse");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"),
                                       store("4"),
                                       jump("tail"),
                                       plain(0x00, "0"),
                                       label("tail"),
                                       recall("4"),
                                       plain(0x0b, "/-/"),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses structural sign sources before sign VP gaps");
    check_ops(result.ops,
              {recall("1", "preload const FACE"), store("4"), jump("tail"), plain(0x00, "0"),
               label("tail"), plain(0x0b, "/-/"), plain(0x0c, "\u0412\u041f"), halt()},
              "structural sign source reuse");
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
                                       jump("tail"),
                                       plain(0x00, "0"),
                                       label("tail"),
                                       recall("4"),
                                       plain(0x0b, "/-/"),
                                       call("transparent"),
                                       plain(0x0c, "\u0412\u041f"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses store-backed sign sources via transparent helper");
    check_ops(result.ops,
              {jump("main"), label("transparent"), plain(0x54, "\u041a\u041d\u041e\u041f"), ret(),
               label("main"), plain(0x01, "1"), plain(0x02, "2"), store("4"), jump("tail"),
               plain(0x00, "0"), label("tail"), plain(0x0b, "/-/"), call("transparent"),
               plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), halt()},
              "store-backed sign source via transparent helper reuse");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), store("4"),
                                       plain(0x0d, "Cx"), plain(0x01, "1"), plain(0x02, "2"),
                                       jump("tail"), plain(0x00, "0"), label("tail"), recall("4"),
                                       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses decimal register memory after X rebuilt across CFG");
    check_ops(result.ops,
              {plain(0x01, "1"), plain(0x02, "2"), store("4"), plain(0x0d, "Cx"), plain(0x01, "1"),
               plain(0x02, "2"), jump("tail"), plain(0x00, "0"), label("tail"), plain(0x20, "F pi"),
               plain(0x0c, "\u0412\u041f"), halt()},
              "decimal register memory across CFG reuse");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), jump("tail"),
                                       plain(0x00, "0"), label("tail"),
                                       recall("4", "preload const 12"), plain(0x20, "F pi"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops preloaded decimal recall after X rebuilt across CFG");
    check_ops(result.ops,
              {plain(0x01, "1"), plain(0x02, "2"), jump("tail"), plain(0x00, "0"), label("tail"),
               plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt()},
              "preloaded decimal recall across CFG drop");
  }

  {
    const std::vector<IrOp> program = {recall("4"), store("5"), recall("4"),
                                       plain(0x0c, "\u0412\u041f"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps redundant X2 sync before immediate VP context");
    check_ops(result.ops, program, "redundant X2 sync before VP preserved");
  }

  {
    const std::vector<IrOp> program = {recall("4"),  cjump("skip"), recall("4"),
                                       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                                       halt(),       label("skip"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses direct conditional fallthrough X2 sync");
    check_ops(result.ops,
              {recall("4"), cjump("skip"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt(),
               label("skip"), halt()},
              "direct conditional fallthrough X2 sync reuse");
  }

  {
    const std::vector<IrOp> program = {recall("4"),  loop("skip"),  recall("4"),
                                       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                                       halt(),       label("skip"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses counted-loop fallthrough X proof for non-counter");
    check_ops(result.ops,
              {recall("4"), loop("skip"), plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"), halt(),
               label("skip"), halt()},
              "counted-loop fallthrough non-counter reuse");
  }

  {
    const std::vector<IrOp> program = {recall("0"),  loop("skip"),  recall("0"),
                                       plain(0x20, "F pi"), plain(0x0c, "\u0412\u041f"),
                                       halt(),       label("skip"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps counted-loop counter fallthrough recalls");
    check_ops(result.ops, program, "counted-loop counter fallthrough preserved");
  }

  {
    const std::vector<IrOp> program = {store("4"),  recall("4"),    call("noop"),
                                       plain(0x0c, "\u0412\u041f"), halt(),
                                       label("noop"), ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall when direct return syncs X2 before VP");
    check_ops(result.ops,
              {store("4"), call("noop"), plain(0x0c, "\u0412\u041f"), halt(), label("noop"), ret()},
              "direct return X2 sync recall removed");
  }

  {
    const std::vector<IrOp> program = {store("4"),
                                       recall("4"),
                                       known_target_indirect_call("7", 5),
                                       plain(0x0c, "\u0412\u041f"),
                                       halt(),
                                       label("noop"),
                                       ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall when known indirect return syncs X2 before VP");
    check_ops(result.ops,
              {store("4"), known_target_indirect_call("7", 5), plain(0x0c, "\u0412\u041f"), halt(),
               label("noop"), ret()},
              "indirect return X2 sync recall removed");
  }

  {
    const std::vector<IrOp> program = {store("4"), recall("4"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall that lifts stack for immediate binary op");
    check_ops(result.ops, program, "stack-lift recall before binary op preserved");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), store("4"), plain(0x0e, "\u0412\u2191"),
                                       recall("4"), plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall before binary op when Y already duplicated");
    check_ops(result.ops,
              {plain(0x01, "1"), store("4"), plain(0x0e, "\u0412\u2191"), plain(0x10, "+"), halt()},
              "recall before binary op with duplicate Y removed");
  }

  {
    const std::vector<IrOp> program = {store("4"), recall("4"), recall("4"), plain(0x10, "+"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "does not use recall producer already removed in same pass");
    check_ops(result.ops, {store("4"), recall("4"), plain(0x10, "+"), halt()},
              "removed producer not reused");
  }

  {
    const std::vector<IrOp> program = {store("4"), recall("4"), plain(0x35, "\u041a {x}"),
                                       plain(0x10, "+"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall whose stack lift survives X-only ops");
    check_ops(result.ops, program, "stack-lift recall surviving X-only ops preserved");
  }

  {
    const std::vector<IrOp> program = {recall("2"),  cjump("false"), recall("2"),
                                       store("3"),   jump("end"),    label("false"),
                                       recall("2"),  store("4"),     label("end"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 2, "drops recalls on both condition paths when X preserved");
    check(count_recall(result.ops, "2") == 1, "both-paths recall count");
  }

  {
    const std::vector<IrOp> program = {recall("1"), cjump("right"), recall("2"), jump("join"),
                                       label("right"), recall("3"), label("join"), recall("2"),
                                       halt()};
    const auto result = run(program);
    const IrOp& second_last = result.ops.at(result.ops.size() - 2U);
    check(second_last.kind == IrKind::Recall && second_last.register_name == "2",
          "merge disagreement keeps recall");
  }

  {
    const std::vector<IrOp> program = {recall("1"),  cjump("join"), call("terminal"),
                                       label("join"), recall("1"),  halt(),
                                       label("terminal"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "ignores direct call continuations unless callee returns");
    check(count_recall(result.ops, "1") == 1, "non-returning call recall count");
  }

  {
    const std::vector<IrOp> program = {recall("4"), call("callee"), jump("end"), label("callee"),
                                       recall("4"), ret(), label("end"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "carries X facts into direct callees");
    check_ops(result.ops,
              {recall("4"), call("callee"), jump("end"), label("callee"), ret(), label("end"),
               halt()},
              "X facts into callee reuse");
  }

  {
    const std::vector<IrOp> program = {call("load"), recall("4"), halt(), label("load"),
                                       recall("4"), ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "carries returned X facts back to direct call continuations");
    check_ops(result.ops, {call("load"), halt(), label("load"), recall("4"), ret()},
              "returned X facts reuse");
  }

  {
    const std::vector<IrOp> program = {recall("4"), known_target_indirect_call("8", 4), jump("end"),
                                       label("callee"), recall("4"), ret(), label("end"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "carries X facts through proved indirect calls");
    check_ops(result.ops,
              {recall("4"), known_target_indirect_call("8", 4), jump("end"), label("callee"), ret(),
               label("end"), halt()},
              "X facts through proved indirect call reuse");
  }

  {
    const std::vector<IrOp> program = {call("choose"), recall("1"), halt(), label("choose"),
                                       cjump("right"), recall("1"), ret(), label("right"),
                                       recall("2"), ret()};
    const auto result = run(program);
    check_applied(result.applied, 0, "intersects disagreeing return-time X facts");
    check_ops(result.ops, program, "disagreeing return-time X facts preserved");
  }

  {
    const std::vector<IrOp> program = {recall("2"),
                                       cjump("target"),
                                       plain(0x54, "\u041a \u041d\u041e\u041f"),
                                       jump("join"),
                                       label("target"),
                                       plain(0x55, "\u041a 1"),
                                       label("join"),
                                       plain(0x56, "\u041a 2"),
                                       recall("2"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "preserves X facts through documented empty operators");
    check_ops(result.ops,
              {recall("2"), cjump("target"), plain(0x54, "\u041a \u041d\u041e\u041f"), jump("join"),
               label("target"), plain(0x55, "\u041a 1"), label("join"), plain(0x56, "\u041a 2"),
               halt()},
              "documented empty operators X reuse");
  }

  {
    const std::vector<IrOp> program = {recall("2"), label("body"), recall("2"), loop("body"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "reuses X across counted loop backedges for non-counter");
    check_ops(result.ops, {recall("2"), label("body"), loop("body"), halt()},
              "counted loop backedge non-counter reuse");
  }

  {
    const std::vector<IrOp> program = {recall("0"), label("body"), recall("0"), loop("body"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps counted-loop counter recalls after loop mutation");
    check_ops(result.ops, program, "counted-loop counter recall preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1"), numeric_jump(3), recall("1"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "follows resolved numeric flow targets when removal safe");
    check_ops(result.ops, {recall("1"), numeric_jump(3), halt()}, "resolved numeric flow reuse");
  }

  {
    const std::vector<IrOp> program = {recall("1"), recall("1"), numeric_jump(4), recall("2"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recalls before later numeric targets");
    check_ops(result.ops, program, "recalls before numeric targets preserved");
  }

  {
    const std::vector<IrOp> program = {recall("1"), indirect_jump("7"), recall("1"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "avoids unknown indirect flow targets");
  }

  // Cases whose native divergence from TS is expected until the X2
  // decimal-digit-run / structural VP-source recall-removal feature is ported.
  const std::set<std::string> deferred = {
      "uses value X2 aliases from literal stores before VP gaps",
      "literal-store X2 alias reuse",
      "uses store-backed sign sources before sign VP gaps",
      "store-backed sign source reuse",
      "uses structural sign sources before sign VP gaps",
      "structural sign source reuse",
      "uses store-backed sign sources via transparent helper",
      "store-backed sign source via transparent helper reuse",
      "uses decimal register memory after X rebuilt across CFG",
      "decimal register memory across CFG reuse",
      "drops preloaded decimal recall after X rebuilt across CFG",
      "preloaded decimal recall across CFG drop",
  };

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
  require(message.empty(), "flow-x-reuse parity divergence set changed:" + message);
}

} // namespace mkpro::tests
