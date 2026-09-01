#include "mkpro/core/passes/entry_stack_input_reuse.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <algorithm>

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
  const auto run_materialization_order = [&](const std::vector<IrOp>& program) {
    return core::passes::call_entry_materialization_order_pass().run(program, ctx);
  };
  const auto run_early_hoist = [&](const std::vector<IrOp>& program) {
    return core::passes::early_helper_invariant_recall_hoist_pass().run(program, ctx);
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
    // Positive: a fully typed multi-target indirect call preserves the
    // caller's current X at every admitted entry. Each helper independently
    // proves that recalling the same value is stack/X2-dead.
    IrOp dispatch = indirect_call("e");
    dispatch.meta.indirect_flow_targets =
        std::vector<IrTarget>{std::string("left"), std::string("right")};
    const std::vector<IrOp> program = {
        recall("4"), dispatch, jump("done"), label("left"), recall("4"), ret(),
        label("right"), recall("4"), ret(), label("done"), halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 2, "typed indirect call seeds every callee entry X");
    check(count_recall(result.ops, "4") == 1,
          "typed indirect call removes both redundant callee recalls");
  }

  {
    // Negative: no call and no stack-resident source for the recall.
    const std::vector<IrOp> program = {plain(0x20, "F pi"), recall("4"), store("5"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps recall without stack-resident source");
    check_ops(result.ops, program, "plain recall preserved");
  }

  {
    // A common recall moved before the first helper opcode is exactly the same
    // instruction order as at every caller, but pays one shared cell instead
    // of one cell per call. Typed unrelated indirect flow remains part of the
    // complete proof map.
    IrOp typed_exit = indirect_jump("e");
    typed_exit.meta.indirect_flow_targets =
        std::vector<IrTarget>{std::string("done")};
    const std::vector<IrOp> program = {
        recall("9"), call("helper"), plain(0x38, "K OR"), store("9"),
        recall("9"), call("helper"), plain(0x37, "K AND"), store("9"),
        recall("9"), call("helper"), plain(0x38, "K OR"), store("9"), jump("done"),
        label("typed_exit"), typed_exit, label("helper"), recall("1"),
        plain(0x22, "F 10^x"), recall("2"), plain(0x10, "+"), ret(),
        label("done"), halt(),
    };
    const auto result = run_early_hoist(program);
    check_applied(result.applied, 1, "early invariant root hoist applies");
    check(result.ops.size() + 2U == program.size(),
          "early invariant root hoist saves two cells across three calls");
    const auto helper = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "helper";
    });
    check(helper != result.ops.end() && std::distance(helper, result.ops.end()) >= 3 &&
              std::next(helper)->kind == IrKind::Recall &&
              std::next(helper)->register_name == "9" &&
              std::next(helper, 2)->kind == IrKind::Recall &&
              std::next(helper, 2)->register_name == "1",
          "early invariant recall inserted before original helper entry");
  }

  {
    // Positive atomic composition. The first materialization is the value
    // recalled at helper entry; putting that pair last leaves R1 in X. The
    // helper's two later recalls and binary drops erase the deeper-stack
    // difference, so the existing recall-removal proof accepts the result.
    const std::vector<IrOp> program = {
        recall("4"), store("1"), recall("5"), store("2"), call("helper"), jump("done"),
        label("helper"), recall("1"), plain(0x22, "F 10^x"), recall("2"), recall("6"),
        plain(0x12, "*"), plain(0x22, "F 10^x"), plain(0x34, "K [x]"),
        plain(0x10, "+"), recall("9"), plain(0x38, "K OR"), store("9"), ret(),
        label("done"), halt(),
    };
    const auto result = run_materialization_order(program);
    check(result.applied >= 2, "materialization order and entry recall both apply");
    check(result.ops.size() + 1U == program.size(),
          "materialization order composition saves one cell");
    check(!result.optimizations.empty() &&
              result.optimizations.front().name == "call-entry-materialization-order",
          "materialization order optimization name");
    check(result.ops.at(0).kind == IrKind::Recall && result.ops.at(0).register_name == "5" &&
              result.ops.at(2).kind == IrKind::Recall && result.ops.at(2).register_name == "4",
          "materialization pairs reordered around independent stores");
    const auto helper = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "helper";
    });
    check(helper != result.ops.end() && std::next(helper) != result.ops.end() &&
              std::next(helper)->kind == IrKind::Plain && std::next(helper)->opcode == 0x22,
          "redundant helper entry recall removed");
  }

  {
    // Negative: storing R1 before recalling it is a real dependency. Swapping
    // the pairs would change R2, so the alias gate must reject atomically.
    const std::vector<IrOp> program = {
        recall("4"), store("1"), recall("1"), store("2"), call("helper"), jump("done"),
        label("helper"), recall("1"), plain(0x22, "F 10^x"), recall("2"), recall("6"),
        plain(0x12, "*"), plain(0x10, "+"), ret(), label("done"), halt(),
    };
    const auto result = run_materialization_order(program);
    check_applied(result.applied, 0, "materialization order rejects register alias");
    check_ops(result.ops, program, "aliased materializations preserved");
  }

  {
    // Negative: although the copies are independent, the helper immediately
    // consumes the stack lift made by its entry recall. The shared bounded
    // proof refuses the composition and therefore also rolls back the swap.
    const std::vector<IrOp> program = {
        recall("4"), store("1"), recall("5"), store("2"), call("helper"), jump("done"),
        label("helper"), recall("1"), plain(0x10, "+"), ret(), label("done"), halt(),
    };
    const auto result = run_materialization_order(program);
    check_applied(result.applied, 0, "materialization order rejects live stack lift");
    check_ops(result.ops, program, "live-lift materializations preserved");
  }

  {
    // Positive three-proof composition. R9 is materialized before two calls
    // and after the third, immediately beside commutative consumers. The
    // shared invariant-recall proof moves it to the helper tail. That exposes
    // R1 in X at the first two calls; reordering the independent copies does
    // the same at the third, and the ordinary entry-stack proof removes the
    // helper's leading R1 recall.
    IrOp typed_exit = indirect_jump("e");
    typed_exit.meta.indirect_flow_targets =
        std::vector<IrTarget>{std::string("done")};
    const std::vector<IrOp> program = {
        recall("4"), store("1"), recall("5"), store("2"), recall("1"), recall("9"),
        call("helper"), plain(0x38, "K OR"), store("9"), recall("0"), recall("0"),
        recall("0"), recall("0"), recall("1"), recall("9"), call("helper"),
        plain(0x37, "K AND"), store("9"), recall("0"), recall("0"), recall("0"),
        recall("0"), recall("4"), store("1"),
        recall("5"), store("2"), call("helper"), recall("9"), plain(0x38, "K OR"),
        store("9"), recall("0"), recall("0"), recall("0"), recall("0"), jump("done"),
        label("typed_exit"), typed_exit, label("helper"),
        recall("1"), plain(0x22, "F 10^x"), recall("2"), recall("6"),
        plain(0x12, "*"), plain(0x22, "F 10^x"), plain(0x34, "K [x]"),
        plain(0x10, "+"), ret(), label("done"), halt(),
    };
    const auto result = run_materialization_order(program);
    check(result.applied >= 2, "tail hoist and materialization order composition applies");
    check(result.ops.size() + 3U == program.size(),
          "tail-hoist composition saves three cells");
    check(std::any_of(result.optimizations.begin(), result.optimizations.end(),
                      [](const core::passes::AppliedOptimization& optimization) {
                        return optimization.name == "helper-invariant-recall-hoist";
                      }),
          "tail-hoist composition reports shared invariant proof");
    const auto helper = std::find_if(result.ops.begin(), result.ops.end(), [](const IrOp& op) {
      return op.kind == IrKind::Label && op.name == "helper";
    });
    check(helper != result.ops.end() && std::next(helper) != result.ops.end() &&
              std::next(helper)->kind == IrKind::Plain && std::next(helper)->opcode == 0x22,
          "tail-hoist composition removes helper entry recall");
    const auto helper_return = std::find_if(
        helper, result.ops.end(), [](const IrOp& op) { return op.kind == IrKind::Return; });
    check(helper_return != result.ops.end() && helper_return != helper &&
              std::prev(helper_return)->kind == IrKind::Recall &&
              std::prev(helper_return)->register_name == "9",
          "common recall moved to helper tail");
  }

  if (!failures.empty()) {
    std::string message = "entry_stack_input_reuse failures:";
    for (const std::string& failure : failures)
      message += "\n  - " + failure;
    require(false, message);
  }
}

} // namespace mkpro::tests
