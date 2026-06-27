#include "mkpro/core/passes/vp_splice.hpp"

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (vp-splice describe block; 95 it(...) blocks
//   across both vp-splice regions). Every vpSplice.run(...) assertion is ported
//   here 1:1.
//
// Coverage note: the native vp_splice pass now delegates to the faithfully
// ported dataflow candidate planner x2PlanVpSpliceCandidatesAt (+ its
// sub-planners: adjacent-VP-boundary, proved-ВП restore/empty runs,
// terminal hard-overwrite/fresh-digit restore runs, adjacent sign pairs,
// exponent separators) matching the TS run() ordering/selection exactly. This
// promoted 38 of the 47 originally-deferred planner cases while keeping the
// shipped example corpus byte-exact. The 9 cases still in `deferred` below
// diverge only because of a *separate* foundation gap in computeX2ValueStates'
// vpEntry*/structural-entry transfer across store-splice and control-flow X2
// syncs (see the comment on the `deferred` set); they are tracked via the
// divergence-ratchet so closing that foundation gap will flag them for
// promotion.

void vp_splice_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};

  std::vector<std::string> failures;
  const auto fail = [&](const std::string& label) { failures.push_back(label); };
  const auto run = [&](const std::vector<IrOp>& p) {
    return core::passes::vp_splice_pass().run(p, ctx);
  };
  const auto role_plain = [](int opcode, std::string mnemonic) {
    IrOp op = plain(opcode, std::move(mnemonic));
    op.meta.roles = {"display-byte"};
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
  const auto check_detail_contains = [&](const mkpro::core::passes::PassResult& result,
                                         const std::string& needle, const std::string& label) {
    if (result.optimizations.empty() ||
        result.optimizations.front().detail.find(needle) == std::string::npos)
      fail(label);
  };

  {
    // vp-splice removes a fractional closed-context sign pair
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a fractional closed-context sign pair");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-splice removes a fractional closed-context sign pair");
  }

  {
    // vp-splice keeps a fractional closed-context sign pair when it shields dot
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a fractional closed-context sign pair when it shields dot");
    check_ops_equal(result.ops, program, "vp-splice keeps a fractional closed-context sign pair when it shields dot");
  }

  {
    // vp-splice removes adjacent exponent sign toggles
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes adjacent exponent sign toggles");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes adjacent exponent sign toggles");
  }

  {
    // vp-splice removes exponent sign toggles after closed decimal X2-sync ВП
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after closed decimal X2-sync ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after closed decimal X2-sync ВП");
  }

  {
    // vp-splice removes exponent sign toggles after a recalled decimal register ВП
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after a recalled decimal register ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after a recalled decimal register ВП");
  }

  {
    // vp-splice removes exponent sign toggles after decimal first-digit ВП splice
    const std::vector<IrOp> program = {
      recall("1", "preload const 3"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x54, "КНОП"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes exponent sign toggles after decimal first-digit ВП splice");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1", "preload const 3"),
      recall("2", "preload const 800"),
      plain(0x14, "←→"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after decimal first-digit ВП splice");
  }

  {
    // vp-splice removes exponent sign toggles after structural X2-sync ВП
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after structural X2-sync ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after structural X2-sync ВП");
  }

  {
    // vp-splice removes exponent sign toggles after dot-restored structural X2 ВП
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after dot-restored structural X2 ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const FACE"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after dot-restored structural X2 ВП");
  }

  {
    // vp-splice removes exponent sign toggles after dot-restored structural exponent source
    const std::vector<IrOp> program = {
      recall("2", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after dot-restored structural exponent source");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const Г"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after dot-restored structural exponent source");
  }

  {
    // vp-splice removes exponent sign toggles after conditional fallthrough X2-sync ВП
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after conditional fallthrough X2-sync ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
      label("done"),
      halt(),
    }), "vp-splice removes exponent sign toggles after conditional fallthrough X2-sync ВП");
  }

  {
    // vp-splice removes non-zero sign pairs after normalized structural arithmetic syncs
    const std::vector<IrOp> conditionalProgram = {
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    };
    const std::vector<IrOp> returnProgram = {
      label("main"),
      call("load"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      ret(),
    };

    check_ops_equal(run(conditionalProgram).ops, (std::vector<IrOp>{
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    }), "vp-splice removes non-zero sign pairs after normalized structural arithmetic syncs");
    check_ops_equal(run(returnProgram).ops, (std::vector<IrOp>{
      label("main"),
      call("load"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      ret(),
    }), "vp-splice removes non-zero sign pairs after normalized structural arithmetic syncs");
  }

  {
    // vp-splice keeps zero sign pairs after normalized structural arithmetic syncs
    const std::vector<IrOp> program = {
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      cjump("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps zero sign pairs after normalized structural arithmetic syncs");
    check_ops_equal(result.ops, program, "vp-splice keeps zero sign pairs after normalized structural arithmetic syncs");
  }

  {
    // vp-splice removes a structural sign pair before ВП after conditional fallthrough X2 sync
    const std::vector<IrOp> program = {
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      cjump("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a structural sign pair before ВП after conditional fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      cjump("done"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    }), "vp-splice removes a structural sign pair before ВП after conditional fallthrough X2 sync");
  }

  {
    // vp-splice removes a structural sign pair before ВП after loop fallthrough X2 sync
    const std::vector<IrOp> program = {
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      loop("done"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a structural sign pair before ВП after loop fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const 8.70Е2-6С"),
      store("1"),
      loop("done"),
      plain(0x0c, "ВП"),
      halt(),
      label("done"),
      halt(),
    }), "vp-splice removes a structural sign pair before ВП after loop fallthrough X2 sync");
  }

  {
    // vp-splice removes a structural sign pair before ВП after direct return X2 sync
    const std::vector<IrOp> program = {
      label("main"),
      call("load"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a structural sign pair before ВП after direct return X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      label("main"),
      call("load"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    }), "vp-splice removes a structural sign pair before ВП after direct return X2 sync");
  }

  {
    // vp-splice removes a structural sign pair before ВП after known indirect return X2 sync
    const std::vector<IrOp> program = {
      label("main"),
      known_target_indirect_call("7", 5),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a structural sign pair before ВП after known indirect return X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      label("main"),
      known_target_indirect_call("7", 5),
      plain(0x0c, "ВП"),
      halt(),
      label("load"),
      recall("2", "preload const 8.70Е2-6С"),
      ret(),
    }), "vp-splice removes a structural sign pair before ВП after known indirect return X2 sync");
  }

  {
    // vp-splice removes a structural sign pair before transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a structural sign pair before transparent return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    }), "vp-splice removes a structural sign pair before transparent return helpers and ВП");
  }

  {
    // vp-splice removes an open mantissa sign pair before transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes an open mantissa sign pair before transparent return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an open mantissa sign pair before transparent return helpers and ВП");
  }

  {
    // vp-splice removes a mixed structural restore run before transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed structural restore run before transparent return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("2", "preload const 8.70Е2-6С"),
      call("transparent"),
      plain(0x0c, "ВП"),
      halt(),
    }), "vp-splice removes a mixed structural restore run before transparent return helpers and ВП");
  }

  {
    // vp-splice removes exponent sign toggles after store-backed decimal ВП splice
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after store-backed decimal ВП splice");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after store-backed decimal ВП splice");
  }

  {
    // vp-splice removes exponent sign toggles after indirect-store-backed decimal ВП splice
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_store("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after indirect-store-backed decimal ВП splice");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_store("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after indirect-store-backed decimal ВП splice");
  }

  {
    // vp-splice removes exponent sign toggles after store-backed structural ВП splice
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after store-backed structural ВП splice");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const FACE"),
      store("1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after store-backed structural ВП splice");
  }

  {
    // vp-splice removes exponent sign toggles after indirect-store-backed structural ВП splice
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      known_target_indirect_store("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes exponent sign toggles after indirect-store-backed structural ВП splice");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const FACE"),
      known_target_indirect_store("7", "1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes exponent sign toggles after indirect-store-backed structural ВП splice");
  }

  {
    // vp-splice keeps closed sign toggles before store-backed ВП when they change the source
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      store("1"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps closed sign toggles before store-backed ВП when they change the source");
    check_ops_equal(result.ops, program, "vp-splice keeps closed sign toggles before store-backed ВП when they change the source");
  }

  {
    // vp-splice keeps closed sign toggles before indirect-store-backed ВП when they change the source
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_store("7", "1"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps closed sign toggles before indirect-store-backed ВП when they change the source");
    check_ops_equal(result.ops, program, "vp-splice keeps closed sign toggles before indirect-store-backed ВП when they change the source");
  }

  {
    // vp-splice removes a full empty run before ВП
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x56, "К2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a full empty run before ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes a full empty run before ВП");
  }

  {
    // vp-splice keeps display-sensitive empty cells before ВП
    const IrOp displayEmpty = plain_with_comment(0x54, "КНОП", "display spacer");
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      displayEmpty,
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps display-sensitive empty cells before ВП");
    check_ops_equal(result.ops, program, "vp-splice keeps display-sensitive empty cells before ВП");
  }

  {
    // vp-splice removes adjacent ВП only after active number-entry ВП
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes adjacent ВП only after active number-entry ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes adjacent ВП only after active number-entry ВП");
  }

  {
    // vp-splice applies ordered candidate stages independently
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice applies ordered candidate stages independently");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice applies ordered candidate stages independently");
    check_detail_contains(result, "Stages: duplicate-vp=1, hard-overwrite-terminal=1.", "vp-splice applies ordered candidate stages independently");
  }

  {
    // vp-splice keeps adjacent ВП after closed-context X2 restore
    const std::vector<IrOp> program = {
      plain(0x0d, "Cx"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps adjacent ВП after closed-context X2 restore");
    check_ops_equal(result.ops, program, "vp-splice keeps adjacent ВП after closed-context X2 restore");
  }

  {
    // vp-splice removes an empty run plus redundant ВП in one segment
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes an empty run plus redundant ВП in one segment");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an empty run plus redundant ВП in one segment");
  }

  {
    // vp-splice keeps an empty run before adjacent ВП after closed-context X2 restore
    const std::vector<IrOp> program = {
      plain(0x0d, "Cx"),
      plain(0x0c, "ВП"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice keeps an empty run before adjacent ВП after closed-context X2 restore");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x0d, "Cx"),
      plain(0x0c, "ВП"),
      plain(0x0c, "ВП"),
      halt(),
    }), "vp-splice keeps an empty run before adjacent ВП after closed-context X2 restore");
  }

  {
    // vp-splice removes an empty run before ВП across marker labels
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      label("entry"),
      plain(0x56, "К2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes an empty run before ВП across marker labels");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      label("marker"),
      label("entry"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an empty run before ВП across marker labels");
  }

  {
    // vp-splice removes an empty run before ВП across orphan address gaps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      orphan_address(54),
      plain(0x55, "К1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes an empty run before ВП across orphan address gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      orphan_address(54),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an empty run before ВП across orphan address gaps");
  }

  {
    // vp-splice removes an empty run before transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty run before transparent return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an empty run before transparent return helpers and ВП");
  }

  {
    // vp-splice removes an empty run before known indirect return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      known_target_indirect_call("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty run before known indirect return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      known_target_indirect_call("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an empty run before known indirect return helpers and ВП");
  }

  {
    // vp-splice removes an empty run before nested transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty run before nested transparent return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("transparent"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      call("outer"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes an empty run before nested transparent return helpers and ВП");
  }

  {
    // vp-splice keeps an empty run before return helpers that restore X2
    const std::vector<IrOp> program = {
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("restore"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps an empty run before return helpers that restore X2");
    check_ops_equal(result.ops, program, "vp-splice keeps an empty run before return helpers that restore X2");
  }

  {
    // vp-splice keeps an empty run before nested return helpers that restore X2
    const std::vector<IrOp> program = {
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("outer"),
      call("restore"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x55, "К1"),
      call("outer"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps an empty run before nested return helpers that restore X2");
    check_ops_equal(result.ops, program, "vp-splice keeps an empty run before nested return helpers that restore X2");
  }

  {
    // vp-splice removes an empty separator after an exponent digit before a non-digit command
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty separator after an exponent digit before a non-digit command");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    }), "vp-splice removes an empty separator after an exponent digit before a non-digit command");
  }

  {
    // vp-splice removes an empty separator after a decimal display-shape VP source exponent digit
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty separator after a decimal display-shape VP source exponent digit");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    }), "vp-splice removes an empty separator after a decimal display-shape VP source exponent digit");
  }

  {
    // vp-splice keeps an empty separator before a following exponent digit
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps an empty separator before a following exponent digit");
    check_ops_equal(result.ops, program, "vp-splice keeps an empty separator before a following exponent digit");
  }

  {
    // vp-splice keeps an empty separator before a labeled exponent digit
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      label("digit"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps an empty separator before a labeled exponent digit");
    check_ops_equal(result.ops, program, "vp-splice keeps an empty separator before a labeled exponent digit");
  }

  {
    // vp-splice removes an empty separator before a labeled non-digit command
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      label("close"),
      plain(0x20, "Fπ"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty separator before a labeled non-digit command");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      label("close"),
      plain(0x20, "Fπ"),
      halt(),
    }), "vp-splice removes an empty separator before a labeled non-digit command");
  }

  {
    // vp-splice removes an empty separator before an orphan address gap and non-digit command
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      orphan_address(54),
      plain(0x20, "Fπ"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty separator before an orphan address gap and non-digit command");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      orphan_address(54),
      plain(0x20, "Fπ"),
      halt(),
    }), "vp-splice removes an empty separator before an orphan address gap and non-digit command");
  }

  {
    // vp-splice keeps an empty separator before an orphan address gap and exponent digit
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x54, "КНОП"),
      orphan_address(54),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps an empty separator before an orphan address gap and exponent digit");
    check_ops_equal(result.ops, program, "vp-splice keeps an empty separator before an orphan address gap and exponent digit");
  }

  {
    // vp-splice removes an empty separator before VP-context sign-change after preserving gaps
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes an empty separator before VP-context sign-change after preserving gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      halt(),
    }), "vp-splice removes an empty separator before VP-context sign-change after preserving gaps");
  }

  {
    // vp-splice removes a VP-context sign pair before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a VP-context sign pair before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes a VP-context sign pair before fresh digit entry");
  }

  {
    // vp-splice removes a VP-context sign pair and empty op before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a VP-context sign pair and empty op before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes a VP-context sign pair and empty op before fresh digit entry");
  }

  {
    // vp-splice removes a single VP-context sign before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes a single VP-context sign before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes a single VP-context sign before fresh digit entry");
  }

  {
    // vp-splice removes a single VP-context sign and empty op before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a single VP-context sign and empty op before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes a single VP-context sign and empty op before fresh digit entry");
  }

  {
    // vp-splice removes a VP-context empty run before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a VP-context empty run before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes a VP-context empty run before fresh digit entry");
  }

  {
    // vp-splice preserves labels while removing a VP-context restore run before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      label("entry"),
      plain(0x0b, "/-/"),
      plain(0x55, "К1"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice preserves labels while removing a VP-context restore run before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      label("entry"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice preserves labels while removing a VP-context restore run before fresh digit entry");
  }

  {
    // vp-splice removes VP-context restore runs before orphan address gaps and fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      orphan_address(54),
      plain(0x55, "К1"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes VP-context restore runs before orphan address gaps and fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      orphan_address(54),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes VP-context restore runs before orphan address gaps and fresh digit entry");
  }

  {
    // vp-splice removes VP-context restore runs before transparent direct-return helpers and fresh digit entry
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      call("transparent"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes VP-context restore runs before transparent direct-return helpers and fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      call("transparent"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes VP-context restore runs before transparent direct-return helpers and fresh digit entry");
  }

  {
    // vp-splice removes VP-context restore runs before proved indirect-return helpers and fresh digit entry
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      known_target_indirect_call("7", 2),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes VP-context restore runs before proved indirect-return helpers and fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      known_target_indirect_call("7", 2),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes VP-context restore runs before proved indirect-return helpers and fresh digit entry");
  }

  {
    // vp-splice keeps VP-context restore runs before direct-return helpers that observe X
    const std::vector<IrOp> program = {
      jump("main"),
      label("observer"),
      store("1"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      call("observer"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps VP-context restore runs before direct-return helpers that observe X");
    check_ops_equal(result.ops, program, "vp-splice keeps VP-context restore runs before direct-return helpers that observe X");
  }

  {
    // vp-splice keeps an active exponent sign before an exponent digit
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps an active exponent sign before an exponent digit");
    check_ops_equal(result.ops, program, "vp-splice keeps an active exponent sign before an exponent digit");
  }

  {
    // vp-splice removes a mixed active exponent restore run before proved ВП
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x04, "4"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed active exponent restore run before proved ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0c, "ВП"),
      plain(0x04, "4"),
      halt(),
    }), "vp-splice removes a mixed active exponent restore run before proved ВП");
  }

  {
    // vp-splice removes a mixed active structural exponent restore run before proved ВП
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed active structural exponent restore run before proved ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0c, "ВП"),
      halt(),
    }), "vp-splice removes a mixed active structural exponent restore run before proved ВП");
  }

  {
    // vp-splice keeps a VP-context sign pair when its X2 restore is observable
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a VP-context sign pair when its X2 restore is observable");
    check_ops_equal(result.ops, program, "vp-splice keeps a VP-context sign pair when its X2 restore is observable");
  }

  {
    // vp-splice keeps a single VP-context sign when its X2 restore is observable
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a single VP-context sign when its X2 restore is observable");
    check_ops_equal(result.ops, program, "vp-splice keeps a single VP-context sign when its X2 restore is observable");
  }

  {
    // vp-splice removes a VP-context sign before a dead X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes a VP-context sign before a dead X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes a VP-context sign before a dead X2 overwrite");
  }

  {
    // vp-splice removes a VP-context sign pair before a dead X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a VP-context sign pair before a dead X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes a VP-context sign pair before a dead X2 overwrite");
  }

  {
    // vp-splice removes a mixed VP-context restore run before a dead X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a mixed VP-context restore run before a dead X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes a mixed VP-context restore run before a dead X2 overwrite");
  }

  {
    // vp-splice removes an active exponent restore run before a dead X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes an active exponent restore run before a dead X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes an active exponent restore run before a dead X2 overwrite");
  }

  {
    // vp-splice removes VP-context empty separators before a dead X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes VP-context empty separators before a dead X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes VP-context empty separators before a dead X2 overwrite");
  }

  {
    // vp-splice removes VP-context empty separators before transparent direct-return helpers and dead overwrite
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      call("transparent"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes VP-context empty separators before transparent direct-return helpers and dead overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      call("transparent"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes VP-context empty separators before transparent direct-return helpers and dead overwrite");
  }

  {
    // vp-splice removes VP-context restore runs before orphan address gaps and dead overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x54, "КНОП"),
      orphan_address(54),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes VP-context restore runs before orphan address gaps and dead overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      orphan_address(54),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes VP-context restore runs before orphan address gaps and dead overwrite");
  }

  {
    // vp-splice removes VP-context empty separators before known indirect-return helpers and dead overwrite
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      known_target_indirect_call("7", 2),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice removes VP-context empty separators before known indirect-return helpers and dead overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      known_target_indirect_call("7", 2),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes VP-context empty separators before known indirect-return helpers and dead overwrite");
  }

  {
    // vp-splice keeps VP-context empty separators before helpers that restore X2
    const std::vector<IrOp> program = {
      jump("main"),
      label("restore"),
      plain(0x0a, "."),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      call("restore"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps VP-context empty separators before helpers that restore X2");
    check_ops_equal(result.ops, program, "vp-splice keeps VP-context empty separators before helpers that restore X2");
  }

  {
    // vp-splice keeps VP-context empty separators before helpers that store X
    const std::vector<IrOp> program = {
      jump("main"),
      label("store_x"),
      store("2"),
      ret(),
      label("main"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x55, "К1"),
      call("store_x"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps VP-context empty separators before helpers that store X");
    check_ops_equal(result.ops, program, "vp-splice keeps VP-context empty separators before helpers that store X");
  }

  {
    // vp-splice removes an active exponent empty separator before a dead X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      plain(0x56, "К2"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes an active exponent empty separator before a dead X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes an active exponent empty separator before a dead X2 overwrite");
  }

  {
    // vp-splice preserves labels while removing proved VP-context empty separators
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x55, "К1"),
      label("entry"),
      plain(0x56, "К2"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice preserves labels while removing proved VP-context empty separators");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      label("entry"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice preserves labels while removing proved VP-context empty separators");
  }

  {
    // vp-splice keeps a no-digit VP-context separator but removes a following sign before fresh digit
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-splice keeps a no-digit VP-context separator but removes a following sign before fresh digit");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice keeps a no-digit VP-context separator but removes a following sign before fresh digit");
  }

  {
    // vp-splice removes closed-context restore runs before fresh digit entry
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes closed-context restore runs before fresh digit entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes closed-context restore runs before fresh digit entry");
  }

  {
    // vp-splice removes closed-context restore runs before hard X2 overwrite
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0d, "Cx"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes closed-context restore runs before hard X2 overwrite");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0d, "Cx"),
      halt(),
    }), "vp-splice removes closed-context restore runs before hard X2 overwrite");
  }

  {
    // vp-splice keeps a closed-context sign before a following ВП
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a closed-context sign before a following ВП");
    check_ops_equal(result.ops, program, "vp-splice keeps a closed-context sign before a following ВП");
  }

  {
    // vp-splice removes a closed-context decimal sign pair
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a closed-context decimal sign pair");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-splice removes a closed-context decimal sign pair");
  }

  {
    // vp-splice removes a closed-context register-valued sign pair
    const std::vector<IrOp> program = {
      recall("1"),
      store("2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a closed-context register-valued sign pair");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      store("2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-splice removes a closed-context register-valued sign pair");
  }

  {
    // vp-splice removes a closed-context opaque sign pair
    const std::vector<IrOp> program = {
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a closed-context opaque sign pair");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x35, "К {x}"),
      plain(0x0e, "В↑"),
      halt(),
    }), "vp-splice removes a closed-context opaque sign pair");
  }

  {
    // vp-splice removes a closed-context structural shape sign pair
    const IrOp shapedSign = role_plain(0x0b, "/-/");
    const std::vector<IrOp> program = {
      recall("2", "preload const 8.70Е2-6С"),
      shapedSign,
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a closed-context structural shape sign pair");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const 8.70Е2-6С"),
      shapedSign,
      halt(),
    }), "vp-splice removes a closed-context structural shape sign pair");
  }

  {
    // vp-splice removes a closed-context structural exponent sign pair
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a closed-context structural exponent sign pair");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-splice removes a closed-context structural exponent sign pair");
  }

  {
    // vp-splice removes a structural sign pair before proved structural ВП entry
    const std::vector<IrOp> program = {
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a structural sign pair before proved structural ВП entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0c, "ВП"),
      halt(),
    }), "vp-splice removes a structural sign pair before proved structural ВП entry");
  }

  {
    // vp-splice removes a mixed structural restore run before proved structural ВП entry
    const std::vector<IrOp> program = {
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed structural restore run before proved structural ВП entry");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x0c, "ВП"),
      halt(),
    }), "vp-splice removes a mixed structural restore run before proved structural ВП entry");
  }

  {
    // vp-splice removes a non-zero closed-context sign pair before proved ВП
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a non-zero closed-context sign pair before proved ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes a non-zero closed-context sign pair before proved ВП");
  }

  {
    // vp-splice removes a mixed closed-context restore run before proved ВП through shared sign source
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed closed-context restore run before proved ВП through shared sign source");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes a mixed closed-context restore run before proved ВП through shared sign source");
  }

  {
    // vp-splice removes a non-zero open mantissa sign pair before proved ВП
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "vp-splice removes a non-zero open mantissa sign pair before proved ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes a non-zero open mantissa sign pair before proved ВП");
  }

  {
    // vp-splice removes a mixed open mantissa restore run before proved ВП
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed open mantissa restore run before proved ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes a mixed open mantissa restore run before proved ВП");
  }

  {
    // vp-splice removes a mixed open mantissa restore run before proved ВП across orphan address gaps
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      orphan_address(54),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "vp-splice removes a mixed open mantissa restore run before proved ВП across orphan address gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      orphan_address(54),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-splice removes a mixed open mantissa restore run before proved ВП across orphan address gaps");
  }

  {
    // vp-splice keeps display-sensitive signs in open mantissa restore runs
    const IrOp displaySign = plain_with_comment(0x0b, "/-/", "display sign");
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      displaySign,
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps display-sensitive signs in open mantissa restore runs");
    check_ops_equal(result.ops, program, "vp-splice keeps display-sensitive signs in open mantissa restore runs");
  }

  {
    // vp-splice keeps a closed-context sign pair when it shapes a following ВП
    const std::vector<IrOp> program = {
      plain(0x0d, "Cx"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a closed-context sign pair when it shapes a following ВП");
    check_ops_equal(result.ops, program, "vp-splice keeps a closed-context sign pair when it shapes a following ВП");
  }

  {
    // vp-splice keeps a zero mantissa sign pair before ВП because signed zero is sticky
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a zero mantissa sign pair before ВП because signed zero is sticky");
    check_ops_equal(result.ops, program, "vp-splice keeps a zero mantissa sign pair before ВП because signed zero is sticky");
  }

  {
    // vp-splice keeps a mixed zero mantissa restore run before ВП because signed zero is sticky
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0b, "/-/"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps a mixed zero mantissa restore run before ВП because signed zero is sticky");
    check_ops_equal(result.ops, program, "vp-splice keeps a mixed zero mantissa restore run before ВП because signed zero is sticky");
  }

  {
    // vp-splice keeps adjacent mantissa sign toggles before a following digit
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0b, "/-/"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-splice keeps adjacent mantissa sign toggles before a following digit");
    check_ops_equal(result.ops, program, "vp-splice keeps adjacent mantissa sign toggles before a following digit");
  }

  // Full parity (0 deferred). The vp-splice candidate planner cluster
  // (x2PlanVpSpliceCandidatesAt + sub-planners) is fully ported, and the
  // foundation computeX2ValueStates transfer now faithfully reproduces TS's
  // vpEntry* / structural-entry tracking across store-splice and control-flow
  // X2 syncs (store -> vpEntryMantissa "0." reset; conditional/loop
  // fallthrough, direct and indirect return, and normalized
  // structural-arithmetic syncs preserving the vpEntry sign source). With the
  // faithful states the planner reaches the same same-source / exponent-context
  // verdict as TS, so the 9 previously-deferred X2-transfer-fidelity cases now
  // pass and have been promoted.
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
  require(message.empty(), "vp-splice parity divergence set changed:" + message);
}

} // namespace mkpro::tests
