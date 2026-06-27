#include "mkpro/core/passes/x2_hidden_temp_restore.hpp"

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/dead_store_elimination.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (x2-hidden-temp-restore assertions, scattered
//   across the large pass describe block; 79 it(...) blocks).
//
// Every x2HiddenTempRestore.run(...) assertion in passes.test.ts is ported here
// 1:1. The native pass consumes the faithful compute_x2_hidden_temp_restore
// decision logic in helpers.cpp, so full parity is expected. Failures are
// collected and checked against an (initially empty) deferred set using the
// divergence-ratchet pattern.
//
// Note: one block ("uses decimal exponent display-shape expr keys") also asserts
// two internal shape-set predicates (x2ShapeSetsHaveSameDotSafeDecimal /
// x2ShapeSetsHaveSameDecimalDisplayShape) that are not part of the native public
// surface; its observable pass behaviour and the public dataflow-shape text are
// ported, which fully covers the optimisation outcome.

void x2_hidden_temp_restore_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};

  std::vector<std::string> failures;
  const auto fail = [&](const std::string& label) { failures.push_back(label); };
  const auto run = [&](const std::vector<IrOp>& p) {
    return core::passes::x2_hidden_temp_restore_pass().run(p, ctx);
  };
  const auto run_dse = [&](const std::vector<IrOp>& p) {
    return core::passes::dead_store_elimination_pass().run(p, ctx);
  };
  const auto is_plain_dot = [](const IrOp& op, int opcode) {
    return op.kind == IrKind::Plain && op.opcode == opcode;
  };
  const auto machine_cell_count = [](const std::vector<IrOp>& ops) {
    int n = 0;
    for (const mkpro::MachineItem& item : mkpro::lower_ir_to_machine(ops))
      if (item.kind != mkpro::MachineItemKind::Label)
        ++n;
    return n;
  };
  const auto dot_restore = [](const std::string& reg) {
    IrOp op;
    op.kind = IrKind::Plain;
    op.opcode = 0x0a;
    op.meta.mnemonic = ".";
    op.meta.comment = "restore " + reg + " from hidden X2 temp";
    return op;
  };
  const auto check_applied = [&](int actual, int expected, const std::string& label) {
    if (actual != expected)
      fail(label);
  };
  const auto check_plain_at = [&](const std::vector<IrOp>& ops, int i, int opcode,
                                  const std::string& label) {
    if (i < 0 || i >= static_cast<int>(ops.size()) ||
        !is_plain_dot(ops[static_cast<std::size_t>(i)], opcode))
      fail(label);
  };
  const auto check_no_store = [&](const std::vector<IrOp>& ops, const std::string& reg,
                                  const std::string& label) {
    for (const IrOp& op : ops)
      if (op.kind == IrKind::Store && op.register_name == reg) {
        fail(label);
        return;
      }
  };
  const auto check_no_indirect_store = [&](const std::vector<IrOp>& ops, const std::string& label) {
    for (const IrOp& op : ops)
      if (op.kind == IrKind::IndirectStore) {
        fail(label);
        return;
      }
  };
  const auto check_has_plain = [&](const std::vector<IrOp>& ops, int opcode,
                                   const std::string& label) {
    for (const IrOp& op : ops)
      if (is_plain_dot(op, opcode))
        return;
    fail(label);
  };
  const auto check_cell_delta = [&](const std::vector<IrOp>& left, const std::vector<IrOp>& right,
                                    int delta, const std::string& label) {
    if (machine_cell_count(left) != machine_cell_count(right) - delta)
      fail(label);
  };
  const auto check_ops_equal = [&](const std::vector<IrOp>& a, const std::vector<IrOp>& b,
                                   const std::string& label) {
    if (mkpro::ir_ops_to_json(a) != mkpro::ir_ops_to_json(b))
      fail(label);
  };
  const auto check_op_index_equal = [&](const std::vector<IrOp>& a, int i,
                                        const std::vector<IrOp>& b, int j,
                                        const std::string& label) {
    if (i < 0 || i >= static_cast<int>(a.size()) || j < 0 || j >= static_cast<int>(b.size()) ||
        mkpro::ir_ops_to_json({a[static_cast<std::size_t>(i)]}) !=
            mkpro::ir_ops_to_json({b[static_cast<std::size_t>(j)]}))
      fail(label);
  };
  const auto check_shape_text =
      [&](const std::vector<std::optional<core::passes::X2ValueDataflowState>>& states, int i,
          bool x2, const std::vector<std::string>& expected, const std::string& label) {
        if (i < 0 || i >= static_cast<int>(states.size()) ||
            !states[static_cast<std::size_t>(i)].has_value()) {
          fail(label);
          return;
        }
        const auto& shape =
            x2 ? states[static_cast<std::size_t>(i)]->x2Shape : states[static_cast<std::size_t>(i)]->xShape;
        const std::vector<std::string> actual(shape.begin(), shape.end());
        if (actual != expected)
          fail(label);
      };

  {
    // x2-hidden-temp-restore uses immediate sync after an X2-preserving scratch alias
    const std::vector<IrOp> program = {
        plain(0x00, "0"),
        store("1"),
        plain(0x54, "\u041a \u041d\u041e\u041f"),
        recall("1"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "x2-hidden-temp-restore uses immediate sync after an X2-preserving scratch alias");
    const std::vector<IrOp> expected = {
        plain(0x00, "0"),
        store("1"),
        plain(0x54, "\u041a \u041d\u041e\u041f"),
        dot_restore("1"),
        halt(),
    };
    check_ops_equal(result.ops, expected,
                    "x2-hidden-temp-restore uses immediate sync after an X2-preserving scratch alias");
  }

  {
    // x2-hidden-temp-restore uses a previous recall lift when X and Y already match
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        recall("1"),
        recall("2"),
        plain(0x10, "+"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "x2-hidden-temp-restore uses a previous recall lift when X and Y already match");
    const std::vector<IrOp> expected = {
        recall("1"),
        store("2"),
        recall("1"),
        dot_restore("2"),
        plain(0x10, "+"),
        halt(),
    };
    check_ops_equal(result.ops, expected,
                    "x2-hidden-temp-restore uses a previous recall lift when X and Y already match");
  }

  {
    // x2-hidden-temp-restore uses normalized structural shapes after path-sensitive syncs
    const std::vector<IrOp> conditional_program = {
        recall("1", "preload const A"),
        plain(0x22, "F x^2"),
        store("2"),
        cjump("done"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const std::vector<IrOp> loop_program = {
        recall("1", "preload const A"),
        plain(0x22, "F x^2"),
        store("2"),
        loop("done"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const std::vector<IrOp> return_program = {
        jump("main"),
        label("sync"),
        plain(0x54, "\u041a \u041d\u041e\u041f"),
        ret(),
        label("main"),
        recall("1", "preload const A"),
        plain(0x22, "F x^2"),
        store("2"),
        call("sync"),
        recall("2"),
        halt(),
    };
    const char* kLabel =
        "x2-hidden-temp-restore uses normalized structural shapes after path-sensitive syncs";
    for (const std::vector<IrOp>& program : {conditional_program, loop_program, return_program}) {
      const auto restored = run(program);
      const auto dse = run_dse(restored.ops);
      check_applied(restored.applied, 1, kLabel);
      check_has_plain(restored.ops, 0x0a, kLabel);
      check_no_store(dse.ops, "2", kLabel);
      check_cell_delta(dse.ops, program, 1, kLabel);
    }
  }

  {
    // x2-hidden-temp-restore uses decimal exponent display-shape expr keys
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0c, "\u0412\u041f"),
        plain(0x08, "8"),
        plain(0x31, "\u041a |x|"),
        store("3"),
        plain(0x01, "1"),
        plain(0x0c, "\u0412\u041f"),
        plain(0x08, "8"),
        plain(0x31, "\u041a |x|"),
        plain(0x0e, "\u0412\u2191"),
        recall("3"),
        halt(),
    };
    const char* kLabel = "x2-hidden-temp-restore uses decimal exponent display-shape expr keys";
    const auto states = core::passes::compute_x2_value_states(
        program, core::passes::X2ValueStatesOptions{.track_register_memory = true});
    const auto result = run(program);
    const auto dse = run_dse(result.ops);
    check_shape_text(states, 4, false, {"exponent:1:8:decimal"}, kLabel);
    check_shape_text(states, 10, true, {"exponent:1:8:decimal"}, kLabel);
    check_applied(result.applied, 1, kLabel);
    check_plain_at(result.ops, 10, 0x0a, kLabel);
    check_no_store(dse.ops, "3", kLabel);
    check_cell_delta(dse.ops, program, 1, kLabel);
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated pure unary computations
    const std::vector<IrOp> program = {
        plain(0x02, "2"),
        plain(0x21, "F sqrt"),
        store("1"),
        plain(0x02, "2"),
        plain(0x21, "F sqrt"),
        plain(0x0e, "В↑"),
        recall("1"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated pure unary computations");
    check_plain_at(result.ops, 6, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated pure unary computations");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated stack-consuming computations
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x10, "+"),
        store("3"),
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x10, "+"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated stack-consuming computations");
    check_plain_at(result.ops, 10, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated stack-consuming computations");
  }

  {
    // x2-hidden-temp-restore uses canonical stable expr keys for commutative computations
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x10, "+"),
        store("3"),
        plain(0x02, "2"),
        plain(0x0e, "В↑"),
        plain(0x01, "1"),
        plain(0x10, "+"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses canonical stable expr keys for commutative computations");
    check_plain_at(result.ops, 10, 0x0a, "x2-hidden-temp-restore uses canonical stable expr keys for commutative computations");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated X/Y exchange computations
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x14, "X↔Y"),
        plain(0x11, "-"),
        store("3"),
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x14, "X↔Y"),
        plain(0x11, "-"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated X/Y exchange computations");
    check_plain_at(result.ops, 12, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated X/Y exchange computations");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated Y->X stack copies
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x3e, "Y->X"),
        plain(0x10, "+"),
        store("3"),
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x3e, "Y->X"),
        plain(0x10, "+"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated Y->X stack copies");
    check_plain_at(result.ops, 12, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated Y->X stack copies");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated Y-keeping computations
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x36, "К max"),
        plain(0x37, "К ∧"),
        store("3"),
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x36, "К max"),
        plain(0x37, "К ∧"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated Y-keeping computations");
    check_plain_at(result.ops, 12, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated Y-keeping computations");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated closed sign-change
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x0b, "/-/"),
        store("2"),
        recall("1"),
        plain(0x0b, "/-/"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated closed sign-change");
    check_plain_at(result.ops, 6, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated closed sign-change");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys from opaque SSA sources
    const std::vector<IrOp> program = {
        plain(0x35, "К {x}"),
        plain(0x0e, "В↑"),
        plain(0x31, "К |x|"),
        store("2"),
        plain(0x0a, "."),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "x2-hidden-temp-restore uses stable expr keys from opaque SSA sources");
    check_plain_at(result.ops, 7, 0x0a, "x2-hidden-temp-restore uses stable expr keys from opaque SSA sources");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable across direct-return helpers
    const std::vector<IrOp> program = {
        jump("main"),
        label("transparent"),
        plain(0x54, "КНОП"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        call("transparent"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct-return helpers");
    check_plain_at(restored.ops, 12, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct-return helpers");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct-return helpers");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct-return helpers");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect-return helpers
    const std::vector<IrOp> program = {
        jump("main"),
        label("transparent"),
        plain(0x54, "КНОП"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        known_target_indirect_call("7", 2),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect-return helpers");
    check_plain_at(restored.ops, 12, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect-return helpers");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect-return helpers");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect-return helpers");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable when helpers read the source
    const std::vector<IrOp> program = {
        jump("main"),
        label("read_source"),
        recall("1"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        call("read_source"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable when helpers read the source");
    check_plain_at(restored.ops, 12, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable when helpers read the source");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable when helpers read the source");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable when helpers read the source");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable when proved indirect helpers read the source
    const std::vector<IrOp> program = {
        jump("main"),
        label("read_source"),
        recall("1"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        known_target_indirect_call("7", 2),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable when proved indirect helpers read the source");
    check_plain_at(restored.ops, 12, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable when proved indirect helpers read the source");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable when proved indirect helpers read the source");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable when proved indirect helpers read the source");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable through nested helpers that read the source
    const std::vector<IrOp> program = {
        jump("main"),
        label("read_source"),
        recall("1"),
        ret(),
        label("outer"),
        call("read_source"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        call("outer"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable through nested helpers that read the source");
    check_plain_at(restored.ops, 15, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable through nested helpers that read the source");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable through nested helpers that read the source");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable through nested helpers that read the source");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr recalls when nested helpers overwrite the source
    const std::vector<IrOp> program = {
        jump("main"),
        label("overwrite_source"),
        plain(0x05, "5"),
        store("1"),
        ret(),
        label("outer"),
        call("overwrite_source"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        call("outer"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps register-dependent expr recalls when nested helpers overwrite the source");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps register-dependent expr recalls when nested helpers overwrite the source");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr recalls when a helper overwrites the source
    const std::vector<IrOp> program = {
        jump("main"),
        label("overwrite_source"),
        plain(0x05, "5"),
        store("1"),
        ret(),
        label("main"),
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        call("overwrite_source"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps register-dependent expr recalls when a helper overwrites the source");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps register-dependent expr recalls when a helper overwrites the source");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable across stable indirect selector reads
    const std::vector<IrOp> program = {
        recall("8"),
        plain(0x31, "К |x|"),
        store("2"),
        known_target_indirect_store("8", "3"),
        recall("8"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across stable indirect selector reads");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable across stable indirect selector reads");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable across stable indirect selector reads");
    check_cell_delta(dse.ops, program, 2, "x2-hidden-temp-restore keeps register-dependent expr keys stable across stable indirect selector reads");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr recalls across mutating indirect selector reads
    const std::vector<IrOp> program = {
        recall("4"),
        plain(0x31, "К |x|"),
        store("2"),
        known_target_indirect_store("4", "3"),
        recall("4"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps register-dependent expr recalls across mutating indirect selector reads");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps register-dependent expr recalls across mutating indirect selector reads");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable across direct conditional fallthroughs
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        cjump("done"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct conditional fallthroughs");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct conditional fallthroughs");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct conditional fallthroughs");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across direct conditional fallthroughs");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable across counted-loop fallthroughs
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        loop("done"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across counted-loop fallthroughs");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable across counted-loop fallthroughs");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable across counted-loop fallthroughs");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across counted-loop fallthroughs");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect conditional fallthroughs
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        known_target_indirect_cjump("7", 9),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect conditional fallthroughs");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect conditional fallthroughs");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect conditional fallthroughs");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore keeps register-dependent expr keys stable across proved indirect conditional fallthroughs");
  }

  {
    // x2-hidden-temp-restore keeps register-dependent expr recalls when a branch overwrites the source
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        cjump("overwrite_source"),
        label("join"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
        label("overwrite_source"),
        plain(0x05, "5"),
        store("1"),
        jump("join"),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps register-dependent expr recalls when a branch overwrites the source");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps register-dependent expr recalls when a branch overwrites the source");
  }

  {
    // x2-hidden-temp-restore keeps expr-key scratch recalls after a source register overwrite
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x31, "К |x|"),
        store("2"),
        plain(0x05, "5"),
        store("1"),
        recall("1"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore keeps expr-key scratch recalls after a source register overwrite");
    check_plain_at(restored.ops, 5, 0x0a, "x2-hidden-temp-restore keeps expr-key scratch recalls after a source register overwrite");
    check_op_index_equal(restored.ops, 8, program, 8, "x2-hidden-temp-restore keeps expr-key scratch recalls after a source register overwrite");
  }

  {
    // x2-hidden-temp-restore uses stable register sources after a later X2 sync
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        plain(0x0d, "Cx"),
        recall("1"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses stable register sources after a later X2 sync");
    check_plain_at(restored.ops, 4, 0x0a, "x2-hidden-temp-restore uses stable register sources after a later X2 sync");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses stable register sources after a later X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses stable register sources after a later X2 sync");
  }

  {
    // x2-hidden-temp-restore keeps register-source recalls after the source register is overwritten
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        plain(0x05, "5"),
        store("1"),
        recall("1"),
        plain(0x10, "+"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps register-source recalls after the source register is overwritten");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps register-source recalls after the source register is overwritten");
  }

  {
    // x2-hidden-temp-restore uses stable expr keys across repeated constant stack producers
    const std::vector<IrOp> program = {
        plain(0x20, "F pi"),
        store("3"),
        plain(0x0d, "Cx"),
        plain(0x20, "F pi"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses stable expr keys across repeated constant stack producers");
    check_plain_at(restored.ops, 5, 0x0a, "x2-hidden-temp-restore uses stable expr keys across repeated constant stack producers");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses stable expr keys across repeated constant stack producers");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses stable expr keys across repeated constant stack producers");
  }

  {
    // x2-hidden-temp-restore uses structural shape expr keys across different registers
    const std::vector<IrOp> program = {
        recall("1", "preload const FABC"),
        plain(0x31, "К |x|"),
        store("3"),
        recall("2", "preload const FABC"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses structural shape expr keys across different registers");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses structural shape expr keys across different registers");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses structural shape expr keys across different registers");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses structural shape expr keys across different registers");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned hex multiply facts after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const A"),
        plain(0x0e, "В↑"),
        plain(0x01, "1"),
        plain(0x08, "8"),
        plain(0x12, "×"),
        store("3"),
        plain(0xf0, "F* empty F0"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned hex multiply facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned hex B multiply facts after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const B"),
        plain(0x0e, "В↑"),
        plain(0x01, "1"),
        plain(0x08, "8"),
        plain(0x12, "×"),
        store("3"),
        plain(0xf0, "F* empty F0"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned hex B multiply facts after X2 sync");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore uses emulator-pinned hex B multiply facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned hex B multiply facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned hex B multiply facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned structural square facts after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const B"),
        plain(0x22, "F x^2"),
        store("3"),
        plain(0x0d, "Cx"),
        recall("2", "preload const 0B"),
        plain(0x22, "F x^2"),
        plain(0xf0, "F* empty F0"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned structural square facts after X2 sync");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore uses emulator-pinned structural square facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned structural square facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned structural square facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned super square facts after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const FA"),
        plain(0x22, "F x^2"),
        store("3"),
        plain(0x0d, "Cx"),
        plain(0x00, "0"),
        plain(0xf0, "F* empty F0"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned super square facts after X2 sync");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses emulator-pinned super square facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned super square facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned super square facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned hex addition facts after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const Г"),
        plain(0x0e, "В↑"),
        plain(0x04, "4"),
        plain(0x10, "+"),
        store("3"),
        plain(0x0d, "Cx"),
        recall("2", "preload const Г"),
        plain(0x0e, "В↑"),
        plain(0x04, "4"),
        plain(0x10, "+"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned hex addition facts after X2 sync");
    check_plain_at(restored.ops, 11, 0x0a, "x2-hidden-temp-restore uses emulator-pinned hex addition facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned hex addition facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned hex addition facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync
    const std::vector<IrOp> program = {
        recall("2", "preload const 1"),
        plain(0x0e, "В↑"),
        recall("1", "preload const ГE-2"),
        plain(0x12, "×"),
        store("3"),
        plain(0xf0, "F* empty F0"),
        recall("3"),
        halt(),
    };
    const std::vector<IrOp> extended_program = {
        recall("1", "preload const AE-2"),
        plain(0x0e, "В↑"),
        recall("2", "preload const 15"),
        plain(0x12, "×"),
        store("3"),
        plain(0xf0, "F* empty F0"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto extended_restored = run(extended_program);
    const auto dse = run_dse(restored.ops);
    const auto extended_dse = run_dse(extended_restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_applied(extended_restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_plain_at(extended_restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_no_store(extended_dse.ops, "3", "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_cell_delta(extended_dse.ops, extended_program, 1, "x2-hidden-temp-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses structural hex sign facts after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const -8F"),
        plain(0x32, "К ЗН"),
        store("3"),
        plain(0x0d, "Cx"),
        recall("2", "preload const -8F"),
        plain(0x32, "К ЗН"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses structural hex sign facts after X2 sync");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore uses structural hex sign facts after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses structural hex sign facts after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses structural hex sign facts after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses structural closed sign-change expr keys after X2 sync
    const std::vector<IrOp> program = {
        recall("1", "preload const FABC"),
        plain(0x0b, "/-/"),
        store("3"),
        recall("2", "preload const FABC"),
        plain(0x0b, "/-/"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses structural closed sign-change expr keys after X2 sync");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses structural closed sign-change expr keys after X2 sync");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses structural closed sign-change expr keys after X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses structural closed sign-change expr keys after X2 sync");
  }

  {
    // x2-hidden-temp-restore uses restored structural shape expr keys after exponent shift
    const std::vector<IrOp> program = {
        recall("1", "preload const Г00"),
        plain(0x31, "К |x|"),
        store("3"),
        recall("2", "preload const Г"),
        plain(0x0c, "ВП"),
        plain(0x02, "2"),
        plain(0x31, "К |x|"),
        plain(0x0e, "В↑"),
        recall("3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses restored structural shape expr keys after exponent shift");
    check_plain_at(restored.ops, 8, 0x0a, "x2-hidden-temp-restore uses restored structural shape expr keys after exponent shift");
    check_no_store(dse.ops, "3", "x2-hidden-temp-restore uses restored structural shape expr keys after exponent shift");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses restored structural shape expr keys after exponent shift");
  }

  {
    // x2-hidden-temp-restore turns closed exponent-entry scratch recalls into dot restores
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x02, "2"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        store("2"),
        plain(0x35, "К {x}"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore turns closed exponent-entry scratch recalls into dot restores");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore turns closed exponent-entry scratch recalls into dot restores");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore turns closed exponent-entry scratch recalls into dot restores");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore turns closed exponent-entry scratch recalls into dot restores");
  }

  {
    // x2-hidden-temp-restore turns opaque scratch recalls into dot restores
    const std::vector<IrOp> program = {
        plain(0x35, "К {x}"),
        plain(0x0e, "В↑"),
        store("2"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore turns opaque scratch recalls into dot restores");
    check_plain_at(restored.ops, 5, 0x0a, "x2-hidden-temp-restore turns opaque scratch recalls into dot restores");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore turns opaque scratch recalls into dot restores");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore turns opaque scratch recalls into dot restores");
  }

  {
    // x2-hidden-temp-restore turns opaque sign-change scratch recalls into dot restores
    const std::vector<IrOp> program = {
        plain(0x35, "К {x}"),
        plain(0x0e, "В↑"),
        plain(0x0b, "/-/"),
        store("2"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore turns opaque sign-change scratch recalls into dot restores");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore turns opaque sign-change scratch recalls into dot restores");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore turns opaque sign-change scratch recalls into dot restores");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore turns opaque sign-change scratch recalls into dot restores");
  }

  {
    // x2-hidden-temp-restore uses pure-unary expr facts after an explicit X2 sync
    const std::vector<IrOp> program = {
        plain(0x02, "2"),
        plain(0x21, "F sqrt"),
        plain(0x0e, "В↑"),
        store("2"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses pure-unary expr facts after an explicit X2 sync");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses pure-unary expr facts after an explicit X2 sync");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses pure-unary expr facts after an explicit X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses pure-unary expr facts after an explicit X2 sync");
  }

  {
    // x2-hidden-temp-restore uses pure stack-consuming expr facts after an explicit X2 sync
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0e, "В↑"),
        plain(0x02, "2"),
        plain(0x10, "+"),
        plain(0x0e, "В↑"),
        store("2"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses pure stack-consuming expr facts after an explicit X2 sync");
    check_plain_at(restored.ops, 8, 0x0a, "x2-hidden-temp-restore uses pure stack-consuming expr facts after an explicit X2 sync");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses pure stack-consuming expr facts after an explicit X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses pure stack-consuming expr facts after an explicit X2 sync");
  }

  {
    // x2-hidden-temp-restore uses stable binary expr keys from recalled expression memory
    const std::vector<IrOp> program = {
        plain(0x35, "К {x}"),
        store("3"),
        recall("1"),
        plain(0x10, "+"),
        store("2"),
        plain(0x0d, "Cx"),
        recall("3"),
        plain(0x0e, "В↑"),
        recall("1"),
        plain(0x10, "+"),
        plain(0x0e, "В↑"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses stable binary expr keys from recalled expression memory");
    check_plain_at(restored.ops, 13, 0x0a, "x2-hidden-temp-restore uses stable binary expr keys from recalled expression memory");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses stable binary expr keys from recalled expression memory");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses stable binary expr keys from recalled expression memory");
  }

  {
    // x2-hidden-temp-restore uses decimal register memory without an X2 register alias
    const std::vector<IrOp> program = {
        plain(0x02, "2"),
        store("1"),
        plain(0x0d, "Cx"),
        plain(0x02, "2"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("1"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses decimal register memory without an X2 register alias");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses decimal register memory without an X2 register alias");
    check_no_store(dse.ops, "1", "x2-hidden-temp-restore uses decimal register memory without an X2 register alias");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses decimal register memory without an X2 register alias");
  }

  {
    // x2-hidden-temp-restore uses fractional decimal scratch values
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0a, "."),
        plain(0x02, "2"),
        store("1"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("1"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses fractional decimal scratch values");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses fractional decimal scratch values");
    check_no_store(dse.ops, "1", "x2-hidden-temp-restore uses fractional decimal scratch values");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses fractional decimal scratch values");
  }

  {
    // x2-hidden-temp-restore uses closed fractional exponent-entry scratch values
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x0a, "."),
        plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        plain(0xf0, "F* empty F0"),
        store("1"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("1"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses closed fractional exponent-entry scratch values");
    check_plain_at(restored.ops, 10, 0x0a, "x2-hidden-temp-restore uses closed fractional exponent-entry scratch values");
    check_no_store(dse.ops, "1", "x2-hidden-temp-restore uses closed fractional exponent-entry scratch values");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses closed fractional exponent-entry scratch values");
  }

  {
    // x2-hidden-temp-restore uses raw leading-zero fractional scratch recalls when only visible X is observed
    const std::vector<IrOp> program = {
        plain(0x00, "0"),
        plain(0x01, "1"),
        plain(0x0a, "."),
        plain(0x02, "2"),
        store("1"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("1"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses raw leading-zero fractional scratch recalls when only visible X is observed");
    check_plain_at(restored.ops, 7, 0x0a, "x2-hidden-temp-restore uses raw leading-zero fractional scratch recalls when only visible X is observed");
    check_no_store(dse.ops, "1", "x2-hidden-temp-restore uses raw leading-zero fractional scratch recalls when only visible X is observed");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses raw leading-zero fractional scratch recalls when only visible X is observed");
  }

  {
    // x2-hidden-temp-restore keeps raw leading-zero fractional scratch recalls before observable VP context
    const std::vector<IrOp> program = {
        plain(0x00, "0"),
        plain(0x01, "1"),
        plain(0x0a, "."),
        plain(0x02, "2"),
        store("1"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("1"),
        plain(0x55, "К 1"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps raw leading-zero fractional scratch recalls before observable VP context");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps raw leading-zero fractional scratch recalls before observable VP context");
  }

  {
    // x2-hidden-temp-restore replaces a safe recall with dot so DSE can remove the scratch store
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        plain(0x35, "К {x}"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore replaces a safe recall with dot so DSE can remove the scratch store");
    check_plain_at(restored.ops, 3, 0x0a, "x2-hidden-temp-restore replaces a safe recall with dot so DSE can remove the scratch store");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore replaces a safe recall with dot so DSE can remove the scratch store");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore replaces a safe recall with dot so DSE can remove the scratch store");
  }

  {
    // x2-hidden-temp-restore uses value dataflow after a dot restore
    const std::vector<IrOp> program = {
        recall("1"),
        plain(0x35, "К {x}"),
        plain(0x54, "К НОП"),
        plain(0x0a, "."),
        store("2"),
        plain(0x35, "К {x}"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses value dataflow after a dot restore");
    check_plain_at(restored.ops, 6, 0x0a, "x2-hidden-temp-restore uses value dataflow after a dot restore");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses value dataflow after a dot restore");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses value dataflow after a dot restore");
  }

  {
    // x2-hidden-temp-restore uses modeled closed sign-change as a dot source
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x02, "2"),
        plain(0x0b, "/-/"),
        store("2"),
        plain(0x20, "Fπ"),
        plain(0x01, "1"),
        plain(0x02, "2"),
        plain(0x0b, "/-/"),
        plain(0x55, "К 1"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses modeled closed sign-change as a dot source");
    check_plain_at(restored.ops, 9, 0x0a, "x2-hidden-temp-restore uses modeled closed sign-change as a dot source");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses modeled closed sign-change as a dot source");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses modeled closed sign-change as a dot source");
  }

  {
    // x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape
    const std::vector<IrOp> program = {
        plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),
        store("2"),
        store("1"),
        recall("2"),
        plain(0x0b, "/-/"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape");
    check_plain_at(restored.ops, 4, 0x0a, "x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape");
    check_no_store(dse.ops, "1", "x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape");
    check_cell_delta(dse.ops, program, 2, "x2-hidden-temp-restore uses store-backed sign ВП source as a dot escape");
  }

  {
    // x2-hidden-temp-restore uses recall VP-shape proof as a dot escape
    const std::vector<IrOp> program = {
        plain(0x01, "1"),
        plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),
        store("2"),
        store("1"),
        recall("2"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses recall VP-shape proof as a dot escape");
    check_plain_at(restored.ops, 5, 0x0a, "x2-hidden-temp-restore uses recall VP-shape proof as a dot escape");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses recall VP-shape proof as a dot escape");
    check_cell_delta(dse.ops, program, 2, "x2-hidden-temp-restore uses recall VP-shape proof as a dot escape");
  }

  {
    // x2-hidden-temp-restore uses structural VP-shape proof as a dot escape
    const std::vector<IrOp> program = {
        recall("1", "preload const FACE"),
        store("2"),
        recall("2"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses structural VP-shape proof as a dot escape");
    check_plain_at(restored.ops, 2, 0x0a, "x2-hidden-temp-restore uses structural VP-shape proof as a dot escape");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses structural VP-shape proof as a dot escape");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses structural VP-shape proof as a dot escape");
  }

  {
    // x2-hidden-temp-restore keeps structural scratch recalls
    const std::vector<IrOp> program = {
        recall("1", "preload const 8.70Е2-6С"),
        store("2"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps structural scratch recalls");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps structural scratch recalls");
  }

  {
    // x2-hidden-temp-restore uses emulator-pinned dot-safe structural scratch recalls
    const std::vector<IrOp> program = {
        recall("1", "preload const C"),
        store("2"),
        plain(0x20, "Fπ"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses emulator-pinned dot-safe structural scratch recalls");
    check_plain_at(restored.ops, 3, 0x0a, "x2-hidden-temp-restore uses emulator-pinned dot-safe structural scratch recalls");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses emulator-pinned dot-safe structural scratch recalls");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses emulator-pinned dot-safe structural scratch recalls");
  }

  {
    // x2-hidden-temp-restore uses dot-safe structural scratch recalls before immediate VP context
    const std::vector<IrOp> program = {
        recall("1", "preload const C"),
        store("2"),
        recall("2"),
        plain(0x0c, "ВП"),
        plain(0x02, "2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses dot-safe structural scratch recalls before immediate VP context");
    check_plain_at(restored.ops, 2, 0x0a, "x2-hidden-temp-restore uses dot-safe structural scratch recalls before immediate VP context");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses dot-safe structural scratch recalls before immediate VP context");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses dot-safe structural scratch recalls before immediate VP context");
  }

  {
    // x2-hidden-temp-restore keeps structural scratch recalls before sign VP context
    const std::vector<IrOp> program = {
        recall("1", "preload const FACE"),
        store("2"),
        recall("2"),
        plain(0x0b, "/-/"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        halt(),
    };
    const auto restored = run(program);
    check_applied(restored.applied, 0, "x2-hidden-temp-restore keeps structural scratch recalls before sign VP context");
    check_ops_equal(restored.ops, program, "x2-hidden-temp-restore keeps structural scratch recalls before sign VP context");
  }

  {
    // x2-hidden-temp-restore handles stable indirect scratch stores and recalls
    const std::vector<IrOp> program = {
        recall("1"),
        known_target_indirect_store("8", "2"),
        plain(0x35, "К {x}"),
        known_target_indirect_recall("8", "2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore handles stable indirect scratch stores and recalls");
    check_plain_at(restored.ops, 3, 0x0a, "x2-hidden-temp-restore handles stable indirect scratch stores and recalls");
    check_no_indirect_store(dse.ops, "x2-hidden-temp-restore handles stable indirect scratch stores and recalls");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore handles stable indirect scratch stores and recalls");
  }

  {
    // x2-hidden-temp-restore crosses unreferenced marker labels
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        label("marker"),
        plain(0x35, "К {x}"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore crosses unreferenced marker labels");
    check_plain_at(restored.ops, 4, 0x0a, "x2-hidden-temp-restore crosses unreferenced marker labels");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore crosses unreferenced marker labels");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore crosses unreferenced marker labels");
  }

  {
    // x2-hidden-temp-restore crosses direct conditional fallthrough X2 sync
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        cjump("done"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore crosses direct conditional fallthrough X2 sync");
    check_plain_at(restored.ops, 3, 0x0a, "x2-hidden-temp-restore crosses direct conditional fallthrough X2 sync");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore crosses direct conditional fallthrough X2 sync");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore crosses direct conditional fallthrough X2 sync");
  }

  {
    // x2-hidden-temp-restore crosses counted-loop fallthrough X2 sync for non-counter scratch
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        loop("done"),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore crosses counted-loop fallthrough X2 sync for non-counter scratch");
    check_plain_at(restored.ops, 3, 0x0a, "x2-hidden-temp-restore crosses counted-loop fallthrough X2 sync for non-counter scratch");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore crosses counted-loop fallthrough X2 sync for non-counter scratch");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore crosses counted-loop fallthrough X2 sync for non-counter scratch");
  }

  {
    // x2-hidden-temp-restore uses computed structural VP-source proofs after path-sensitive syncs
    const std::vector<IrOp> program = {
        recall("1", "preload const A"),
        plain(0x22, "F x^2"),
        store("2"),
        cjump("done"),
        recall("2"),
        plain(0x0c, "ВП"),
        plain(0x03, "3"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore uses computed structural VP-source proofs after path-sensitive syncs");
    check_plain_at(restored.ops, 4, 0x0a, "x2-hidden-temp-restore uses computed structural VP-source proofs after path-sensitive syncs");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore uses computed structural VP-source proofs after path-sensitive syncs");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore uses computed structural VP-source proofs after path-sensitive syncs");
  }

  {
    // x2-hidden-temp-restore crosses stable known indirect conditional fallthrough
    const std::vector<IrOp> program = {
        plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),
        store("2"),
        known_target_indirect_cjump("7", 6),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore crosses stable known indirect conditional fallthrough");
    check_plain_at(restored.ops, 4, 0x0a, "x2-hidden-temp-restore crosses stable known indirect conditional fallthrough");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore crosses stable known indirect conditional fallthrough");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore crosses stable known indirect conditional fallthrough");
  }

  {
    // x2-hidden-temp-restore keeps recalls when an indirect conditional mutates the scratch selector
    const std::vector<IrOp> program = {
        plain(0x02, "2"),
        store("2"),
        known_target_indirect_cjump("2", 5),
        recall("2"),
        halt(),
        label("done"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps recalls when an indirect conditional mutates the scratch selector");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps recalls when an indirect conditional mutates the scratch selector");
  }

  {
    // x2-hidden-temp-restore crosses simple direct-return X2 syncs that ignore scratch
    const std::vector<IrOp> program = {
        jump("main"),
        label("sync"),
        plain(0x0d, "Cx"),
        ret(),
        label("main"),
        plain(0x0d, "Cx"),
        store("2"),
        call("sync"),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore crosses simple direct-return X2 syncs that ignore scratch");
    check_plain_at(restored.ops, 8, 0x0a, "x2-hidden-temp-restore crosses simple direct-return X2 syncs that ignore scratch");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore crosses simple direct-return X2 syncs that ignore scratch");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore crosses simple direct-return X2 syncs that ignore scratch");
  }

  {
    // x2-hidden-temp-restore crosses known indirect-return X2 syncs that ignore scratch
    const std::vector<IrOp> program = {
        jump("main"),
        label("sync"),
        plain(0x0d, "Cx"),
        ret(),
        label("main"),
        plain(0x0d, "Cx"),
        store("2"),
        known_target_indirect_call("7", 2),
        recall("2"),
        halt(),
    };
    const auto restored = run(program);
    const auto dse = run_dse(restored.ops);
    check_applied(restored.applied, 1, "x2-hidden-temp-restore crosses known indirect-return X2 syncs that ignore scratch");
    check_plain_at(restored.ops, 8, 0x0a, "x2-hidden-temp-restore crosses known indirect-return X2 syncs that ignore scratch");
    check_no_store(dse.ops, "2", "x2-hidden-temp-restore crosses known indirect-return X2 syncs that ignore scratch");
    check_cell_delta(dse.ops, program, 1, "x2-hidden-temp-restore crosses known indirect-return X2 syncs that ignore scratch");
  }

  {
    // x2-hidden-temp-restore keeps recalls when a direct-return callee reads scratch
    const std::vector<IrOp> program = {
        jump("main"),
        label("use_scratch"),
        recall("2"),
        ret(),
        label("main"),
        recall("1"),
        store("2"),
        call("use_scratch"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps recalls when a direct-return callee reads scratch");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps recalls when a direct-return callee reads scratch");
  }

  {
    // x2-hidden-temp-restore keeps recalls across unknown indirect memory in direct-return callees
    const std::vector<IrOp> program = {
        jump("main"),
        label("maybe_reads_scratch"),
        indirect_recall("7"),
        plain(0x0d, "Cx"),
        ret(),
        label("main"),
        plain(0x0d, "Cx"),
        store("2"),
        call("maybe_reads_scratch"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps recalls across unknown indirect memory in direct-return callees");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps recalls across unknown indirect memory in direct-return callees");
  }

  {
    // x2-hidden-temp-restore keeps counted-loop counter scratch recalls
    const std::vector<IrOp> program = {
        recall("1"),
        store("0"),
        loop("done"),
        recall("0"),
        halt(),
        label("done"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps counted-loop counter scratch recalls");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps counted-loop counter scratch recalls");
  }

  {
    // x2-hidden-temp-restore keeps recalls across referenced labels
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        jump("entry"),
        label("entry"),
        plain(0x35, "К {x}"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps recalls across referenced labels");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps recalls across referenced labels");
  }

  {
    // x2-hidden-temp-restore requires a safe dot restore gap
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore requires a safe dot restore gap");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore requires a safe dot restore gap");
  }

  {
    // x2-hidden-temp-restore ignores repeated state recalls that do not free a scratch store
    const std::vector<IrOp> program = {
        store("2"),
        recall("2"),
        plain(0x20, "F pi"),
        plain(0x35, "К {x}"),
        recall("2"),
        store("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore ignores repeated state recalls that do not free a scratch store");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore ignores repeated state recalls that do not free a scratch store");
  }

  {
    // x2-hidden-temp-restore does not use stale aliases after an overwrite
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        plain(0x35, "К {x}"),
        store("2"),
        plain(0x54, "К НОП"),
        recall("2"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore does not use stale aliases after an overwrite");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore does not use stale aliases after an overwrite");
  }

  {
    // x2-hidden-temp-restore keeps recalls whose stack lift is consumed
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        plain(0x35, "К {x}"),
        recall("2"),
        plain(0x10, "+"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps recalls whose stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps recalls whose stack lift is consumed");
  }

  {
    // x2-hidden-temp-restore keeps duplicate-Y recalls without a previous stack/X2 producer
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        plain(0x20, "Fπ"),
        plain(0x20, "Fπ"),
        recall("2"),
        plain(0x10, "+"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps duplicate-Y recalls without a previous stack/X2 producer");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps duplicate-Y recalls without a previous stack/X2 producer");
  }

  {
    // x2-hidden-temp-restore keeps duplicate-Y recalls when deeper stack state is consumed
    const std::vector<IrOp> program = {
        recall("1"),
        store("2"),
        recall("1"),
        recall("2"),
        plain(0x10, "+"),
        plain(0x10, "+"),
        halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "x2-hidden-temp-restore keeps duplicate-Y recalls when deeper stack state is consumed");
    check_ops_equal(result.ops, program, "x2-hidden-temp-restore keeps duplicate-Y recalls when deeper stack state is consumed");
  }


  // Full parity is expected now that the X2 dataflow foundation is live; the
  // deferred set is empty. Any unexpected divergence (or a deferred case that
  // starts passing) trips the ratchet below.
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
  require(message.empty(), "x2-hidden-temp-restore parity divergence set changed:" + message);
}

} // namespace mkpro::tests
