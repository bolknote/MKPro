#include "mkpro/core/passes/entry_stack_input_reuse.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

// Direct unit tests for the entry-stack-input-reuse pass on hand-built IR.
// The pass seeds a proved-transparent callee entry with the caller's pre-call
// Y/Z/T facts and deletes recalls whose value provably already reaches the
// point in X. Every firing must be a pure recall deletion.

void entry_stack_input_reuse_removes_only_proved_entry_stack_recalls() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::entry_stack_input_reuse_pass().run(program, ctx);
  };
  std::vector<std::string> failures;
  const auto check_applied = [&](int actual, int expected, const std::string& label) {
    if (actual != expected)
      failures.push_back(label + ": expected applied=" + std::to_string(expected) + ", got " +
                         std::to_string(actual));
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

  {
    // Positive: the caller leaves R4's value on Y at the call site; the
    // transparent callee swaps it into X and recalls R4 redundantly.
    const std::vector<IrOp> program = {recall("4"), recall("5"),        call("helper"),
                                       jump("done"), label("helper"),   plain(0x14, "<->"),
                                       recall("4"),  store("6"),        ret(),
                                       label("done"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "drops recall of caller Y inside transparent callee");
    check(!result.optimizations.empty() &&
              result.optimizations.at(0).name == "entry-stack-input-reuse",
          "entry stack optimization name");
    check(count_recall(result.ops, "4") == 1, "caller Y recall count");
    const std::vector<IrOp> expected = {recall("4"), recall("5"),      call("helper"),
                                        jump("done"), label("helper"), plain(0x14, "<->"),
                                        store("6"),   ret(),           label("done"),
                                        halt()};
    check_ops(result.ops, expected, "caller Y seeded callee ops");
  }

  {
    // Positive: depth tracking. The callee lifts a temporary, drops it with a
    // binary op (never digging below the entry stack), then swaps the still
    // seeded caller Y fact into X.
    const std::vector<IrOp> program = {recall("4"),   recall("5"),     call("helper"),
                                       jump("done"),  label("helper"), recall("0"),
                                       plain(0x10, "+"), plain(0x14, "<->"), recall("4"),
                                       store("6"),    ret(),           label("done"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "tracks caller Y through balanced lift and drop");
    check(count_recall(result.ops, "4") == 1, "balanced lift-drop recall count");
  }

  {
    // Negative: the callee runs an Exposes-class stack rotation, so the
    // transparency gate rejects the seed and nothing fires.
    const std::vector<IrOp> program = {recall("4"),  recall("5"),     call("helper"),
                                       jump("done"), label("helper"), plain(0x25, "F reverse"),
                                       plain(0x14, "<->"), recall("4"), store("6"),
                                       ret(),        label("done"),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "rejects non-transparent callee stack rotation");
    check_ops(result.ops, program, "non-transparent callee preserved");
  }

  {
    // Negative: the callee digs below the entry stack depth with a binary op,
    // so the transparency gate rejects the seed.
    const std::vector<IrOp> program = {recall("4"),  recall("5"),     call("helper"),
                                       jump("done"), label("helper"), plain(0x10, "+"),
                                       plain(0x14, "<->"), recall("4"), store("6"),
                                       ret(),        label("done"),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "rejects callee that drops below entry stack depth");
    check_ops(result.ops, program, "entry-depth-consuming callee preserved");
  }

  {
    // Negative: the seeded fact is clobbered inside the callee before the
    // recall (unary op overwrites X after the swap).
    const std::vector<IrOp> program = {recall("4"),  recall("5"),     call("helper"),
                                       jump("done"), label("helper"), plain(0x14, "<->"),
                                       plain(0x1c, "F sin"), recall("4"), store("6"),
                                       ret(),        label("done"),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall clobbered inside callee");
    check_ops(result.ops, program, "clobbered callee value preserved");
  }

  {
    // Negative: unknown indirect flow anywhere in the program disables the
    // whole pass (mirrors the flow-x-reuse guard).
    const std::vector<IrOp> program = {recall("4"),  recall("5"),     call("helper"),
                                       jump("done"), label("helper"), plain(0x14, "<->"),
                                       recall("4"),  store("6"),      ret(),
                                       label("done"), indirect_jump("8"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "refuses unknown indirect flow targets");
    check_ops(result.ops, program, "unknown indirect flow preserved");
  }

  {
    // Negative: no call and no stack-resident source for the recall.
    const std::vector<IrOp> program = {plain(0x20, "F pi"), recall("4"), store("5"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall without stack-resident source");
    check_ops(result.ops, program, "plain recall preserved");
  }

  if (!failures.empty()) {
    std::string message = "entry_stack_input_reuse failures:";
    for (const std::string& failure : failures)
      message += "\n  - " + failure;
    require(false, message);
  }
}

} // namespace mkpro::tests
