#include "mkpro/core/passes/x2_literal_restore.hpp"

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/passes/x2_noop_restore.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (x2-literal-restore describe block; 140
//   it(...) blocks). Every x2LiteralRestore.run(...) assertion is ported here
//   1:1 (program builders + applied/ops expectations).
//
// The native pass consumes the faithful x2_literal_restore_replacements
// decision logic in helpers.cpp, so full parity is expected. Failures are
// collected and checked against an (initially empty) deferred set using the
// divergence-ratchet pattern.
//
// Note: one block ("keeps numeric entry while structural VP context is
// active") additionally asserts two internal state-text predicates
// (x2StructuralVpContextStateText / x2StateIsClosedPlainContext) that are not
// part of the native public surface; its observable pass behaviour (applied=0,
// ops unchanged) is ported, which fully covers the optimisation outcome.

void x2_literal_restore_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};

  std::vector<std::string> failures;
  const auto fail = [&](const std::string& label) { failures.push_back(label); };
  const auto run = [&](const std::vector<IrOp>& p) {
    return core::passes::x2_literal_restore_pass().run(p, ctx);
  };
  const auto run_noop = [&](const std::vector<IrOp>& p) {
    return core::passes::x2_noop_restore_pass().run(p, ctx);
  };
  const auto lit_dot = [](const std::string& comment) {
    IrOp op;
    op.kind = IrKind::Plain;
    op.opcode = 0x0a;
    op.meta.mnemonic = ".";
    op.meta.comment = comment;
    return op;
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

  {
    // x2-literal-restore replaces a repeated normalized digit run with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated normalized digit run with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated normalized digit run with dot");
  }

  {
    // x2-literal-restore removes transparent helper gaps after repeated digit runs before explicit sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore removes transparent helper gaps after repeated digit runs before explicit sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore removes transparent helper gaps after repeated digit runs before explicit sync");
  }

  {
    // x2-literal-restore removes proved indirect helper gaps after repeated digit runs before explicit sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      known_target_indirect_call("7", 2),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore removes proved indirect helper gaps after repeated digit runs before explicit sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore removes proved indirect helper gaps after repeated digit runs before explicit sync");
  }

  {
    // x2-literal-restore keeps side-effect helper gaps after repeated digit runs
    const std::vector<IrOp> program = {
      jump("main"),
      label("side_effect"),
      store("1"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      call("side_effect"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore keeps side-effect helper gaps after repeated digit runs");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("side_effect"),
      store("1"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      call("side_effect"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "x2-literal-restore keeps side-effect helper gaps after repeated digit runs");
  }

  {
    // x2-literal-restore replaces a repeated fractional digit run with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces a repeated fractional digit run with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 1.2 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated fractional digit run with dot");
  }

  {
    // x2-literal-restore replaces a repeated signed fractional digit run with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces a repeated signed fractional digit run with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal -1.2 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed fractional digit run with dot");
  }

  {
    // x2-literal-restore replaces a repeated literal after a recalled decimal register
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated literal after a recalled decimal register");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      store("1"),
      plain(0x0d, "Cx"),
      recall("1"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated literal after a recalled decimal register");
  }

  {
    // x2-literal-restore replaces a repeated literal after a preloaded decimal recall
    const std::vector<IrOp> program = {
      recall("2", "preload const 12"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated literal after a preloaded decimal recall");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("2", "preload const 12"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated literal after a preloaded decimal recall");
  }

  {
    // x2-literal-restore keeps repeated literals after hex-like preloaded recalls
    const std::vector<IrOp> program = {
      recall("2", "preload const 8.70Е2-6С"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated literals after hex-like preloaded recalls");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated literals after hex-like preloaded recalls");
  }

  {
    // x2-literal-restore keeps numeric entry while structural VP context is active
    const std::vector<IrOp> program = {
      recall("2", "preload const FACE"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x03, "3"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps numeric entry while structural VP context is active");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps numeric entry while structural VP context is active");
  }

  {
    // x2-literal-restore replaces a repeated leading-zero digit run with dot
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated leading-zero digit run with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 02 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated leading-zero digit run with dot");
  }

  {
    // x2-literal-restore replaces a repeated signed leading-zero digit run with dot
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces a repeated signed leading-zero digit run with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal -02 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed leading-zero digit run with dot");
  }

  {
    // x2-literal-restore replaces leading-zero runs after X2 normalization when only visible X is observed
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces leading-zero runs after X2 normalization when only visible X is observed");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 02 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces leading-zero runs after X2 normalization when only visible X is observed");
  }

  {
    // x2-literal-restore replaces signed leading-zero runs after X2 normalization
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces signed leading-zero runs after X2 normalization");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -02 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces signed leading-zero runs after X2 normalization");
  }

  {
    // x2-literal-restore replaces a repeated digit run immediately after an X2 sync
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated digit run immediately after an X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated digit run immediately after an X2 sync");
  }

  {
    // x2-literal-restore replaces a repeated signed digit run with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces a repeated signed digit run with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal -12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed digit run with dot");
  }

  {
    // x2-literal-restore replaces a signed run after modeled sign-change through empty ops
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces a signed run after modeled sign-change through empty ops");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      lit_dot("restore literal -12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a signed run after modeled sign-change through empty ops");
  }

  {
    // x2-literal-restore keeps signed runs after role-bearing empty ops
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x0b, "/-/"),
      role_plain(0x55, "К 1"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps signed runs after role-bearing empty ops");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps signed runs after role-bearing empty ops");
  }

  {
    // x2-literal-restore replaces a signed digit run immediately after an X2 sync
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces a signed digit run immediately after an X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a signed digit run immediately after an X2 sync");
  }

  {
    // x2-literal-restore replaces a repeated signed single digit with dot
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated signed single digit with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal -5 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed single digit with dot");
  }

  {
    // x2-literal-restore replaces a repeated positive integer exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces a repeated positive integer exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 5000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated positive integer exponent literal with dot");
  }

  {
    // x2-literal-restore replaces a repeated decimal display shape in a closed VP context
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      store("2"),
      store("3"),
      plain(0x05, "5"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces a repeated decimal display shape in a closed VP context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      store("2"),
      store("3"),
      lit_dot("restore literal 5000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated decimal display shape in a closed VP context");
  }

  {
    // x2-literal-restore replaces a repeated fractional-mantissa exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 4, "x2-literal-restore replaces a repeated fractional-mantissa exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 1200 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated fractional-mantissa exponent literal with dot");
  }

  {
    // x2-literal-restore keeps repeated exponent literals while VP context is still observable
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated exponent literals while VP context is still observable");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated exponent literals while VP context is still observable");
  }

  {
    // x2-literal-restore replaces repeated literals before a redundant context restore through a gap
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated literals before a redundant context restore through a gap");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      halt(),
    }), "x2-literal-restore replaces repeated literals before a redundant context restore through a gap");
  }

  {
    // x2-literal-restore keeps repeated literals before an immediate context restore
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated literals before an immediate context restore");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated literals before an immediate context restore");
  }

  {
    // x2-literal-restore replaces a repeated fractional exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces a repeated fractional exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.005 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated fractional exponent literal with dot");
  }

  {
    // x2-literal-restore replaces a repeated signed fractional-mantissa exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 5, "x2-literal-restore replaces a repeated signed fractional-mantissa exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -1200 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed fractional-mantissa exponent literal with dot");
  }

  {
    // x2-literal-restore replaces a repeated fractional-mantissa negative-exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 5, "x2-literal-restore replaces a repeated fractional-mantissa negative-exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.0012 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated fractional-mantissa negative-exponent literal with dot");
  }

  {
    // x2-literal-restore replaces a repeated signed-mantissa exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces a repeated signed-mantissa exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -5000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed-mantissa exponent literal with dot");
  }

  {
    // x2-literal-restore replaces a repeated signed fractional exponent literal with dot
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 4, "x2-literal-restore replaces a repeated signed fractional exponent literal with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -0.005 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated signed fractional exponent literal with dot");
  }

  {
    // x2-literal-restore replaces a repeated leading-zero exponent literal after it is closed
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces a repeated leading-zero exponent literal after it is closed");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 5000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces a repeated leading-zero exponent literal after it is closed");
  }

  {
    // x2-literal-restore keeps positive single digits because dot would not save a cell
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x05, "5"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps positive single digits because dot would not save a cell");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps positive single digits because dot would not save a cell");
  }

  {
    // x2-literal-restore uses path-sensitive conditional fallthrough X2 sync
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses path-sensitive conditional fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore uses path-sensitive conditional fallthrough X2 sync");
  }

  {
    // x2-literal-restore uses path-sensitive loop fallthrough X2 sync
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      loop("done"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses path-sensitive loop fallthrough X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      loop("done"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore uses path-sensitive loop fallthrough X2 sync");
  }

  {
    // x2-literal-restore uses proved indirect conditional preserved normalized X2
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      known_target_indirect_cjump("7", 6),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses proved indirect conditional preserved normalized X2");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      known_target_indirect_cjump("7", 6),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore uses proved indirect conditional preserved normalized X2");
  }

  {
    // x2-literal-restore uses visible leading-zero literals through proved indirect conditionals
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      known_target_indirect_cjump("7", 6),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses visible leading-zero literals through proved indirect conditionals");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      known_target_indirect_cjump("7", 6),
      lit_dot("restore literal 02 from hidden X2 temp"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore uses visible leading-zero literals through proved indirect conditionals");
  }

  {
    // x2-literal-restore uses direct return X2 sync
    const std::vector<IrOp> program = {
      label("main"),
      call("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses direct return X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      label("main"),
      call("load"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    }), "x2-literal-restore uses direct return X2 sync");
  }

  {
    // x2-literal-restore uses known indirect return X2 sync
    const std::vector<IrOp> program = {
      label("main"),
      known_target_indirect_call("7", 4),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses known indirect return X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      label("main"),
      known_target_indirect_call("7", 4),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
      label("load"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      ret(),
    }), "x2-literal-restore uses known indirect return X2 sync");
  }

  {
    // x2-literal-restore keeps conditional fallthrough literals whose stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps conditional fallthrough literals whose stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps conditional fallthrough literals whose stack lift is consumed");
  }

  {
    // x2-literal-restore keeps a signed digit run when its stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps a signed digit run when its stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps a signed digit run when its stack lift is consumed");
  }

  {
    // x2-literal-restore keeps its inserted X2-sensitive dot explicit
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto restored = run(program);
    const auto nooped = run_noop(restored.ops);

    check_applied(restored.applied, 1, "x2-literal-restore keeps its inserted X2-sensitive dot explicit");
    check_applied(nooped.applied, 0, "x2-literal-restore keeps its inserted X2-sensitive dot explicit");
    check_ops_equal(nooped.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore keeps its inserted X2-sensitive dot explicit");
  }

  {
    // x2-literal-restore keeps a repeated digit run when its stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps a repeated digit run when its stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps a repeated digit run when its stack lift is consumed");
  }

  {
    // x2-literal-restore uses a previous stack lift when X and Y already match
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses a previous stack lift when X and Y already match");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x10, "+"),
      halt(),
    }), "x2-literal-restore uses a previous stack lift when X and Y already match");
  }

  {
    // x2-literal-restore keeps duplicate-Y literals when deeper stack state is consumed
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps duplicate-Y literals when deeper stack state is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps duplicate-Y literals when deeper stack state is consumed");
  }

  {
    // x2-literal-restore replaces repeated synced pure unary expressions with dot
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced pure unary expressions with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced pure unary expressions with dot");
  }

  {
    // x2-literal-restore replaces repeated synced pure unary expressions across empty sync gaps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces repeated synced pure unary expressions across empty sync gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced pure unary expressions across empty sync gaps");
  }

  {
    // x2-literal-restore replaces repeated synced chained pure unary expressions
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 4, "x2-literal-restore replaces repeated synced chained pure unary expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      lit_dot("restore literal F sqrt(F sqrt(2)) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced chained pure unary expressions");
  }

  {
    // x2-literal-restore replaces repeated synced pure unary constant expressions
    const std::vector<IrOp> program = {
      plain(0x20, "F pi"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x20, "F pi"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces repeated synced pure unary constant expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x20, "F pi"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(F pi) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced pure unary constant expressions");
  }

  {
    // x2-literal-restore replaces repeated synced pure unary exponent-entry expressions
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 4, "x2-literal-restore replaces repeated synced pure unary exponent-entry expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(5000) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced pure unary exponent-entry expressions");
  }

  {
    // x2-literal-restore replaces repeated synced pure unary register expressions
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      recall("1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced pure unary register expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(R1) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced pure unary register expressions");
  }

  {
    // x2-literal-restore replaces repeated synced pure unary stable-indirect register expressions
    const std::vector<IrOp> program = {
      known_target_indirect_recall("8", "1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      known_target_indirect_recall("8", "1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced pure unary stable-indirect register expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      known_target_indirect_recall("8", "1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("indirect-memory-target=1; restore literal F sqrt(R1) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced pure unary stable-indirect register expressions");
  }

  {
    // x2-literal-restore crosses removable transparent return helpers before explicit expression sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      call("transparent"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore crosses removable transparent return helpers before explicit expression sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore crosses removable transparent return helpers before explicit expression sync");
  }

  {
    // x2-literal-restore crosses removable proved indirect return helpers before explicit expression sync
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_call("7", 2),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore crosses removable proved indirect return helpers before explicit expression sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore crosses removable proved indirect return helpers before explicit expression sync");
  }

  {
    // x2-literal-restore keeps return helper expression gaps with side effects
    const std::vector<IrOp> program = {
      jump("main"),
      label("side_effect"),
      store("1"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      call("side_effect"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps return helper expression gaps with side effects");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps return helper expression gaps with side effects");
  }

  {
    // x2-literal-restore keeps mutating indirect register expressions
    const std::vector<IrOp> program = {
      known_target_indirect_recall("1", "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      known_target_indirect_recall("1", "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps mutating indirect register expressions");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps mutating indirect register expressions");
  }

  {
    // x2-literal-restore keeps register expressions after the source register changes
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x0d, "Cx"),
      store("1"),
      recall("1"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps register expressions after the source register changes");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps register expressions after the source register changes");
  }

  {
    // x2-literal-restore replaces pure unary constant expressions without a later sync at a terminal boundary
    const std::vector<IrOp> program = {
      plain(0x20, "F pi"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x20, "F pi"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces pure unary constant expressions without a later sync at a terminal boundary");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x20, "F pi"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(F pi) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces pure unary constant expressions without a later sync at a terminal boundary");
  }

  {
    // x2-literal-restore replaces repeated synced binary expressions with dot
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 4, "x2-literal-restore replaces repeated synced binary expressions with dot");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal /(1,3) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced binary expressions with dot");
  }

  {
    // x2-literal-restore replaces repeated synced binary expressions with unary tails
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 6, "x2-literal-restore replaces repeated synced binary expressions with unary tails");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      lit_dot("restore literal F sqrt(+(1,3)) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced binary expressions with unary tails");
  }

  {
    // x2-literal-restore replaces repeated synced binary expressions with unary operands
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 6, "x2-literal-restore replaces repeated synced binary expressions with unary operands");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal +(F sqrt(2),F sqrt(3)) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced binary expressions with unary operands");
  }

  {
    // x2-literal-restore replaces repeated synced binary expressions with exponent-entry operands
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 6, "x2-literal-restore replaces repeated synced binary expressions with exponent-entry operands");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal +(5000,2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced binary expressions with exponent-entry operands");
  }

  {
    // x2-literal-restore replaces repeated synced RPN expressions across source and separator gaps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      plain(0x03, "3"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      plain(0x02, "2"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      plain(0x03, "3"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 10, "x2-literal-restore replaces repeated synced RPN expressions across source and separator gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x54, "К НОП"),
      plain(0x03, "3"),
      plain(0x54, "К НОП"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0x54, "К НОП"),
      plain(0xf0, "F* empty F0"),
      plain(0x55, "К 1"),
      lit_dot("restore literal +(F sqrt(2),F sqrt(3)) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced RPN expressions across source and separator gaps");
  }

  {
    // x2-literal-restore replaces repeated synced nested RPN expressions
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 10, "x2-literal-restore replaces repeated synced nested RPN expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal +(+(F sqrt(2),F sqrt(3)),F sqrt(4)) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced nested RPN expressions");
  }

  {
    // x2-literal-restore keeps repeated synced nested RPN expressions when their stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated synced nested RPN expressions when their stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated synced nested RPN expressions when their stack lift is consumed");
  }

  {
    // x2-literal-restore replaces repeated synced binary register expressions
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x0e, "В↑"),
      recall("2"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      recall("1"),
      plain(0x0e, "В↑"),
      recall("2"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 4, "x2-literal-restore replaces repeated synced binary register expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x0e, "В↑"),
      recall("2"),
      plain(0x10, "+"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal +(R1,R2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated synced binary register expressions");
  }

  {
    // x2-literal-restore keeps repeated synced binary expressions when their stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x13, "/"),
      plain(0xf0, "F* empty F0"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated synced binary expressions when their stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated synced binary expressions when their stack lift is consumed");
  }

  {
    // x2-literal-restore replaces pure unary expressions across empty gaps without a later sync at a terminal boundary
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0x54, "К НОП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces pure unary expressions across empty gaps without a later sync at a terminal boundary");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces pure unary expressions across empty gaps without a later sync at a terminal boundary");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions without an explicit result sync at a terminal boundary
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions without an explicit result sync at a terminal boundary");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces repeated pure unary expressions without an explicit result sync at a terminal boundary");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before direct terminal jumps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      jump("done"),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before direct terminal jumps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      jump("done"),
      label("done"),
      halt(),
    }), "x2-literal-restore replaces repeated pure unary expressions before direct terminal jumps");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before terminal conditionals
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before terminal conditionals");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore replaces repeated pure unary expressions before terminal conditionals");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before terminal loops
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      loop("done"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before terminal loops");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      loop("done"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore replaces repeated pure unary expressions before terminal loops");
  }

  {
    // x2-literal-restore keeps repeated pure unary expressions before non-terminal jumps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      jump("consumer"),
      label("consumer"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated pure unary expressions before non-terminal jumps");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated pure unary expressions before non-terminal jumps");
  }

  {
    // x2-literal-restore keeps repeated pure unary expressions before non-terminal conditionals
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      cjump("consumer"),
      halt(),
      label("consumer"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated pure unary expressions before non-terminal conditionals");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated pure unary expressions before non-terminal conditionals");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before backward numeric terminal jumps
    const std::vector<IrOp> program = {
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      numeric_jump(2),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before backward numeric terminal jumps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      numeric_jump(2),
    }), "x2-literal-restore replaces repeated pure unary expressions before backward numeric terminal jumps");
  }

  {
    // x2-literal-restore keeps repeated pure unary expressions before forward numeric terminal jumps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      numeric_jump(8),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated pure unary expressions before forward numeric terminal jumps");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated pure unary expressions before forward numeric terminal jumps");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before backward numeric terminal conditionals
    const std::vector<IrOp> program = {
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      numeric_cjump(2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before backward numeric terminal conditionals");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      numeric_cjump(2),
      halt(),
    }), "x2-literal-restore replaces repeated pure unary expressions before backward numeric terminal conditionals");
  }

  {
    // x2-literal-restore keeps repeated pure unary expressions before forward numeric terminal conditionals
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      numeric_cjump(8),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated pure unary expressions before forward numeric terminal conditionals");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated pure unary expressions before forward numeric terminal conditionals");
  }

  {
    // x2-literal-restore replaces repeated synced expressions before backward numeric conditionals
    const std::vector<IrOp> program = {
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      numeric_cjump(2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced expressions before backward numeric conditionals");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      numeric_cjump(2),
      halt(),
    }), "x2-literal-restore replaces repeated synced expressions before backward numeric conditionals");
  }

  {
    // x2-literal-restore replaces repeated synced expressions before backward numeric calls
    const std::vector<IrOp> program = {
      jump("main"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      numeric_call(2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced expressions before backward numeric calls");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      numeric_call(2),
      halt(),
    }), "x2-literal-restore replaces repeated synced expressions before backward numeric calls");
  }

  {
    // x2-literal-restore keeps repeated synced expressions before forward numeric conditionals
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      numeric_cjump(9),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated synced expressions before forward numeric conditionals");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated synced expressions before forward numeric conditionals");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before backward proved indirect terminal conditionals
    const std::vector<IrOp> program = {
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_cjump("7", 2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before backward proved indirect terminal conditionals");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      known_target_indirect_cjump("7", 2),
      halt(),
    }), "x2-literal-restore replaces repeated pure unary expressions before backward proved indirect terminal conditionals");
  }

  {
    // x2-literal-restore keeps repeated pure unary expressions before forward proved indirect terminal conditionals
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_cjump("7", 7),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated pure unary expressions before forward proved indirect terminal conditionals");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated pure unary expressions before forward proved indirect terminal conditionals");
  }

  {
    // x2-literal-restore replaces repeated synced expressions before backward proved indirect conditionals
    const std::vector<IrOp> program = {
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_cjump("7", 2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced expressions before backward proved indirect conditionals");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      known_target_indirect_cjump("7", 2),
      halt(),
    }), "x2-literal-restore replaces repeated synced expressions before backward proved indirect conditionals");
  }

  {
    // x2-literal-restore replaces repeated synced expressions before backward proved indirect calls
    const std::vector<IrOp> program = {
      jump("main"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_call("7", 2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces repeated synced expressions before backward proved indirect calls");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      known_target_indirect_call("7", 2),
      halt(),
    }), "x2-literal-restore replaces repeated synced expressions before backward proved indirect calls");
  }

  {
    // x2-literal-restore keeps repeated synced expressions before forward proved indirect conditionals
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_cjump("7", 8),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated synced expressions before forward proved indirect conditionals");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated synced expressions before forward proved indirect conditionals");
  }

  {
    // x2-literal-restore keeps repeated synced expressions before forward proved indirect calls
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      known_target_indirect_call("7", 8),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated synced expressions before forward proved indirect calls");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated synced expressions before forward proved indirect calls");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before backward terminal indirect jumps
    const std::vector<IrOp> program = {
      jump("main"),
      label("done"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_jump("7", 2),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before backward terminal indirect jumps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("done"),
      halt(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      known_target_indirect_jump("7", 2),
    }), "x2-literal-restore replaces repeated pure unary expressions before backward terminal indirect jumps");
  }

  {
    // x2-literal-restore keeps repeated pure unary expressions before forward terminal indirect jumps
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_jump("7", 7),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated pure unary expressions before forward terminal indirect jumps");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated pure unary expressions before forward terminal indirect jumps");
  }

  {
    // x2-literal-restore crosses transparent return helpers before terminal expressions
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      call("transparent"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore crosses transparent return helpers before terminal expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore crosses transparent return helpers before terminal expressions");
  }

  {
    // x2-literal-restore crosses backward proved indirect transparent return helpers before terminal expressions
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_call("7", 2),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore crosses backward proved indirect transparent return helpers before terminal expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "К НОП"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore crosses backward proved indirect transparent return helpers before terminal expressions");
  }

  {
    // x2-literal-restore keeps expressions before forward proved indirect return helpers
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      known_target_indirect_call("7", 8),
      halt(),
      plain(0x54, "К НОП"),
      ret(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps expressions before forward proved indirect return helpers");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps expressions before forward proved indirect return helpers");
  }

  {
    // x2-literal-restore keeps expressions before stack-observing return helpers
    const std::vector<IrOp> program = {
      jump("main"),
      label("consumer"),
      plain(0x10, "+"),
      ret(),
      label("main"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      call("consumer"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps expressions before stack-observing return helpers");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps expressions before stack-observing return helpers");
  }

  {
    // x2-literal-restore replaces repeated pure unary expressions before direct return
    const std::vector<IrOp> program = {
      call("value"),
      halt(),
      proc_start("value"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      ret(),
      proc_end("value"),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces repeated pure unary expressions before direct return");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      call("value"),
      halt(),
      proc_start("value"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      ret(),
      proc_end("value"),
    }), "x2-literal-restore replaces repeated pure unary expressions before direct return");
  }

  {
    // x2-literal-restore crosses orphan address gaps before return terminal expressions
    const std::vector<IrOp> program = {
      call("value"),
      halt(),
      proc_start("value"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      orphan_address(77),
      ret(),
      proc_end("value"),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore crosses orphan address gaps before return terminal expressions");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      call("value"),
      halt(),
      proc_start("value"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      lit_dot("restore literal F sqrt(2) from hidden X2 temp"),
      ret(),
      proc_end("value"),
    }), "x2-literal-restore crosses orphan address gaps before return terminal expressions");
  }

  {
    // x2-literal-restore keeps return-bound expressions when their stack lift reaches the caller
    const std::vector<IrOp> program = {
      call("value"),
      plain(0x10, "+"),
      halt(),
      proc_start("value"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      ret(),
      proc_end("value"),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps return-bound expressions when their stack lift reaches the caller");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps return-bound expressions when their stack lift reaches the caller");
  }

  {
    // x2-literal-restore keeps repeated synced pure unary expressions when their stack lift is consumed
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x55, "К 1"),
      plain(0x02, "2"),
      plain(0x21, "F sqrt"),
      plain(0xf0, "F* empty F0"),
      plain(0x10, "+"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps repeated synced pure unary expressions when their stack lift is consumed");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps repeated synced pure unary expressions when their stack lift is consumed");
  }

  {
    // x2-literal-restore replaces visible leading-zero digit runs through preserving gaps
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "Fπ"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces visible leading-zero digit runs through preserving gaps");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 02 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore replaces visible leading-zero digit runs through preserving gaps");
  }

  {
    // x2-literal-restore keeps visible leading-zero digit runs before closed sign restores
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x54, "КНОП"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps visible leading-zero digit runs before closed sign restores");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps visible leading-zero digit runs before closed sign restores");
  }

  {
    // x2-literal-restore replaces digit runs before immediate ВП context after X2-preserving ops
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces digit runs before immediate ВП context after X2-preserving ops");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x0c, "ВП"),
      halt(),
    }), "x2-literal-restore replaces digit runs before immediate ВП context after X2-preserving ops");
  }

  {
    // x2-literal-restore keeps fractional digit runs that would change a following ВП context
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps fractional digit runs that would change a following ВП context");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps fractional digit runs that would change a following ВП context");
  }

  {
    // x2-literal-restore keeps digit runs that would change ВП context through preserving gaps
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps digit runs that would change ВП context through preserving gaps");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps digit runs that would change ВП context through preserving gaps");
  }

  {
    // x2-literal-restore replaces normalized digit runs before empty-op ВП context
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces normalized digit runs before empty-op ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "x2-literal-restore replaces normalized digit runs before empty-op ВП context");
  }

  {
    // x2-literal-restore replaces normalized digit runs before immediate ВП context
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces normalized digit runs before immediate ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "x2-literal-restore replaces normalized digit runs before immediate ВП context");
  }

  {
    // x2-literal-restore replaces normalized digit runs before transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces normalized digit runs before transparent return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x55, "К 1"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "x2-literal-restore replaces normalized digit runs before transparent return helpers and ВП");
  }

  {
    // x2-literal-restore replaces normalized digit runs before proved indirect return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      known_target_indirect_call("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces normalized digit runs before proved indirect return helpers and ВП");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      plain(0x55, "К 1"),
      known_target_indirect_call("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "x2-literal-restore replaces normalized digit runs before proved indirect return helpers and ВП");
  }

  {
    // x2-literal-restore keeps leading-zero digit runs before transparent return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      call("transparent"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps leading-zero digit runs before transparent return helpers and ВП");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps leading-zero digit runs before transparent return helpers and ВП");
  }

  {
    // x2-literal-restore keeps leading-zero digit runs before proved indirect return helpers and ВП
    const std::vector<IrOp> program = {
      jump("main"),
      label("transparent"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      known_target_indirect_call("7", 2),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps leading-zero digit runs before proved indirect return helpers and ВП");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps leading-zero digit runs before proved indirect return helpers and ВП");
  }

  {
    // x2-literal-restore replaces signed digit runs before restore-run ВП context
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces signed digit runs before restore-run ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -12 from hidden X2 temp"),
      plain(0x54, "К НОП"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "x2-literal-restore replaces signed digit runs before restore-run ВП context");
  }

  {
    // x2-literal-restore keeps leading-zero digit runs before empty-op ВП context
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps leading-zero digit runs before empty-op ВП context");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps leading-zero digit runs before empty-op ВП context");
  }

  {
    // x2-literal-restore keeps digit runs before role-bearing empty-op ВП context
    const IrOp roleEmpty = role_plain(0x55, "К 1");
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      roleEmpty,
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps digit runs before role-bearing empty-op ВП context");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps digit runs before role-bearing empty-op ВП context");
  }

  {
    // x2-literal-restore keeps digit runs before role-bearing sign-change ВП context
    const IrOp roleSign = role_plain(0x0b, "/-/");
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      roleSign,
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps digit runs before role-bearing sign-change ВП context");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps digit runs before role-bearing sign-change ВП context");
  }

  {
    // x2-literal-restore replaces exponent literals before empty-op ВП context
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces exponent literals before empty-op ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 5000 from hidden X2 temp"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    }), "x2-literal-restore replaces exponent literals before empty-op ВП context");
  }

  {
    // x2-literal-restore replaces exponent literals before immediate ВП context
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore replaces exponent literals before immediate ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 5000 from hidden X2 temp"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    }), "x2-literal-restore replaces exponent literals before immediate ВП context");
  }

  {
    // x2-literal-restore replaces signed-mantissa exponent literals before empty-op ВП context
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces signed-mantissa exponent literals before empty-op ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal -5000 from hidden X2 temp"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    }), "x2-literal-restore replaces signed-mantissa exponent literals before empty-op ВП context");
  }

  {
    // x2-literal-restore replaces fractional exponent literals before empty-op ВП context
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore replaces fractional exponent literals before empty-op ВП context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.005 from hidden X2 temp"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    }), "x2-literal-restore replaces fractional exponent literals before empty-op ВП context");
  }

  {
    // x2-literal-restore keeps leading-zero exponent literals before empty-op ВП context
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x55, "К 1"),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps leading-zero exponent literals before empty-op ВП context");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps leading-zero exponent literals before empty-op ВП context");
  }

  {
    // x2-literal-restore replaces a repeated literal before a branch with no reachable X2 restore
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore replaces a repeated literal before a branch with no reachable X2 restore");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      cjump("done"),
      halt(),
      label("done"),
      halt(),
    }), "x2-literal-restore replaces a repeated literal before a branch with no reachable X2 restore");
  }

  {
    // x2-literal-restore uses concrete integer-part X2 facts from К [x]
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses concrete integer-part X2 facts from К [x]");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0a, "."),
      plain(0x03, "3"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete integer-part X2 facts from К [x]");
  }

  {
    // x2-literal-restore uses concrete abs X2 facts from К |x|
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses concrete abs X2 facts from К |x|");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 1.2 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete abs X2 facts from К |x|");
  }

  {
    // x2-literal-restore uses concrete binary addition X2 facts
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x05, "5"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses concrete binary addition X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x10, "+"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 15 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete binary addition X2 facts");
  }

  {
    // x2-literal-restore uses concrete multiply and finite division X2 facts
    const std::vector<IrOp> multiplyProgram = {
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x06, "6"),
      plain(0x12, "*"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const std::vector<IrOp> divisionProgram = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x13, "/"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      halt(),
    };
    const auto multiplyResult = run(multiplyProgram);
    const auto divisionResult = run(divisionProgram);

    check_applied(multiplyResult.applied, 1, "x2-literal-restore uses concrete multiply and finite division X2 facts");
    check_ops_equal(multiplyResult.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x06, "6"),
      plain(0x12, "*"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete multiply and finite division X2 facts");
    check_applied(divisionResult.applied, 3, "x2-literal-restore uses concrete multiply and finite division X2 facts");
    check_ops_equal(divisionResult.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x04, "4"),
      plain(0x13, "/"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 0.25 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete multiply and finite division X2 facts");
  }

  {
    // x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync
    const std::vector<IrOp> hexInYProgram = {
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x02, "2"),
      plain(0x00, "0"),
      halt(),
    };
    const std::vector<IrOp> hexInXProgram = {
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x00, "0"),
      halt(),
    };
    const std::vector<IrOp> hexBInYProgram = {
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x05, "5"),
      plain(0x04, "4"),
      halt(),
    };
    const auto hexInYResult = run(hexInYProgram);
    const auto hexInXResult = run(hexInXProgram);
    const auto hexBInYResult = run(hexBInYProgram);

    check_applied(hexInYResult.applied, 1, "x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_ops_equal(hexInYResult.ops, (std::vector<IrOp>{
      recall("1", "preload const A"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 20 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_applied(hexInXResult.applied, 2, "x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_ops_equal(hexInXResult.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x0e, "В↑"),
      recall("1", "preload const A"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 180 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_applied(hexBInYResult.applied, 1, "x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync");
    check_ops_equal(hexBInYResult.ops, (std::vector<IrOp>{
      recall("1", "preload const B"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x08, "8"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 54 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex multiply facts after X2 sync");
  }

  {
    // x2-literal-restore uses structural hex sign facts after X2 sync
    const std::vector<IrOp> program = {
      recall("1", "preload const -8F"),
      plain(0x32, "К ЗН"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses structural hex sign facts after X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1", "preload const -8F"),
      plain(0x32, "К ЗН"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal -1 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses structural hex sign facts after X2 sync");
  }

  {
    // x2-literal-restore uses structural square facts after normalized X2 sync
    const std::vector<IrOp> bSquareProgram = {
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      halt(),
    };
    const std::vector<IrOp> aSquareProgram = {
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      halt(),
    };
    const std::vector<IrOp> scaledBSquareProgram = {
      recall("1", "preload const B0"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      halt(),
    };
    const std::vector<IrOp> scaledBExponentSquareProgram = {
      recall("1", "preload const BE-2"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x01, "1"),
      halt(),
    };
    const auto bSquareResult = run(bSquareProgram);
    const auto aSquareResult = run(aSquareProgram);
    const auto scaledBSquareResult = run(scaledBSquareProgram);
    const auto scaledBExponentSquareResult = run(scaledBExponentSquareProgram);

    check_applied(bSquareResult.applied, 1, "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_ops_equal(bSquareResult.ops, (std::vector<IrOp>{
      recall("1", "preload const B"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 10 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_applied(aSquareResult.applied, 1, "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_ops_equal(aSquareResult.ops, (std::vector<IrOp>{
      recall("1", "preload const A"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 00 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_applied(scaledBSquareResult.applied, 3, "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_ops_equal(scaledBSquareResult.ops, (std::vector<IrOp>{
      recall("1", "preload const B0"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 1000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_applied(scaledBExponentSquareResult.applied, 4, "x2-literal-restore uses structural square facts after normalized X2 sync");
    check_ops_equal(scaledBExponentSquareResult.ops, (std::vector<IrOp>{
      recall("1", "preload const BE-2"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.001 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses structural square facts after normalized X2 sync");
  }

  {
    // x2-literal-restore uses super square zero facts after X2 sync
    const std::vector<IrOp> program = {
      recall("1", "preload const FA"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses super square zero facts after X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1", "preload const FA"),
      plain(0x22, "F x^2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses super square zero facts after X2 sync");
  }

  {
    // x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync
    const std::vector<IrOp> decimalLeftProgram = {
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x01, "1"),
      halt(),
    };
    const std::vector<IrOp> hexLeftProgram = {
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x03, "3"),
      halt(),
    };
    const std::vector<IrOp> extendedHexLeftProgram = {
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 15"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x09, "9"),
      plain(0x0a, "."),
      plain(0x09, "9"),
      halt(),
    };
    const std::vector<IrOp> extendedDecimalLeftProgram = {
      recall("2", "preload const 17"),
      plain(0x0e, "В↑"),
      recall("1", "preload const CE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x07, "7"),
      halt(),
    };
    const auto decimalLeftResult = run(decimalLeftProgram);
    const auto hexLeftResult = run(hexLeftProgram);
    const auto extendedHexLeftResult = run(extendedHexLeftProgram);
    const auto extendedDecimalLeftResult = run(extendedDecimalLeftProgram);

    check_applied(decimalLeftResult.applied, 2, "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_ops_equal(decimalLeftResult.ops, (std::vector<IrOp>{
      recall("2", "preload const 1"),
      plain(0x0e, "В↑"),
      recall("1", "preload const ГE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.1 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_applied(hexLeftResult.applied, 3, "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_ops_equal(hexLeftResult.ops, (std::vector<IrOp>{
      recall("1", "preload const ГE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 5"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 0.53 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_applied(extendedHexLeftResult.applied, 2, "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_ops_equal(extendedHexLeftResult.ops, (std::vector<IrOp>{
      recall("1", "preload const AE-2"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 15"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 9.9 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_applied(extendedDecimalLeftResult.applied, 2, "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
    check_ops_equal(extendedDecimalLeftResult.ops, (std::vector<IrOp>{
      recall("2", "preload const 17"),
      plain(0x0e, "В↑"),
      recall("1", "preload const CE-2"),
      plain(0x12, "×"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 1.7 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses emulator-pinned hex exponent multiply facts after X2 sync");
  }

  {
    // x2-literal-restore uses concrete К max X2 facts
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x05, "5"),
      plain(0x36, "К max"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "x2-literal-restore uses concrete К max X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0e, "В↑"),
      plain(0x05, "5"),
      plain(0x36, "К max"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete К max X2 facts");
  }

  {
    // x2-literal-restore uses concrete unary arithmetic X2 facts
    const std::vector<IrOp> squareProgram = {
      plain(0x04, "4"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x06, "6"),
      halt(),
    };
    const std::vector<IrOp> reciprocalProgram = {
      plain(0x04, "4"),
      plain(0x23, "F 1/x"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      halt(),
    };
    const std::vector<IrOp> sqrtProgram = {
      plain(0x01, "1"),
      plain(0x04, "4"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      halt(),
    };
    const std::vector<IrOp> pow10Program = {
      plain(0x03, "3"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      plain(0x00, "0"),
      halt(),
    };
    const auto squareResult = run(squareProgram);
    const auto reciprocalResult = run(reciprocalProgram);
    const auto sqrtResult = run(sqrtProgram);
    const auto pow10Result = run(pow10Program);

    check_applied(squareResult.applied, 1, "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_ops_equal(squareResult.ops, (std::vector<IrOp>{
      plain(0x04, "4"),
      plain(0x22, "F x^2"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 16 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_applied(reciprocalResult.applied, 3, "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_ops_equal(reciprocalResult.ops, (std::vector<IrOp>{
      plain(0x04, "4"),
      plain(0x23, "F 1/x"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 0.25 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_applied(sqrtResult.applied, 1, "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_ops_equal(sqrtResult.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x04, "4"),
      plain(0x04, "4"),
      plain(0x21, "F sqrt"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 12 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_applied(pow10Result.applied, 3, "x2-literal-restore uses concrete unary arithmetic X2 facts");
    check_ops_equal(pow10Result.ops, (std::vector<IrOp>{
      plain(0x03, "3"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 1000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete unary arithmetic X2 facts");
  }

  {
    // x2-literal-restore uses concrete F pi derived X2 facts
    const std::vector<IrOp> program = {
      plain(0x20, "F pi"),
      plain(0x34, "К [x]"),
      plain(0x0e, "В↑"),
      plain(0x03, "3"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses concrete F pi derived X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x20, "F pi"),
      plain(0x34, "К [x]"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 3.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete F pi derived X2 facts");
  }

  {
    // x2-literal-restore uses exact documented function special-case X2 facts
    const std::vector<IrOp> cosZeroProgram = {
      plain(0x00, "0"),
      plain(0x1d, "F cos"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const std::vector<IrOp> powIdentityProgram = {
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto cosZeroResult = run(cosZeroProgram);
    const auto powIdentityResult = run(powIdentityProgram);

    check_applied(cosZeroResult.applied, 2, "x2-literal-restore uses exact documented function special-case X2 facts");
    check_ops_equal(cosZeroResult.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x1d, "F cos"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 1.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses exact documented function special-case X2 facts");
    check_applied(powIdentityResult.applied, 2, "x2-literal-restore uses exact documented function special-case X2 facts");
    check_ops_equal(powIdentityResult.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 1.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses exact documented function special-case X2 facts");
  }

  {
    // x2-literal-restore uses exact F x^y zero-base X2 facts
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses exact F x^y zero-base X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x0e, "В↑"),
      plain(0x00, "0"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 0.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses exact F x^y zero-base X2 facts");
  }

  {
    // x2-literal-restore uses exact F x^y exponent-one X2 facts
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses exact F x^y exponent-one X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x24, "F x^y"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 1.2 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses exact F x^y exponent-one X2 facts");
  }

  {
    // x2-literal-restore uses scientific exact decimal X2 facts
    const std::vector<IrOp> program = {
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses scientific exact decimal X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 100000000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses scientific exact decimal X2 facts");
  }

  {
    // x2-literal-restore uses scientific VP-source X2 facts after a closing sync
    const std::vector<IrOp> program = {
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x01, "1"),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore uses scientific VP-source X2 facts after a closing sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x08, "8"),
      plain(0x15, "F 10^x"),
      plain(0xf0, "F* empty F0"),
      plain(0x0a, "."),
      plain(0x0c, "ВП"),
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      lit_dot("restore literal 10000000000 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses scientific VP-source X2 facts after a closing sync");
  }

  {
    // x2-literal-restore uses values copied through Y->X after a later X2 sync
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses values copied through Y->X after a later X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x3e, "Y->X"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 1.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses values copied through Y->X after a later X2 sync");
  }

  {
    // x2-literal-restore uses concrete MK-61 bitwise X2 facts
    const std::vector<IrOp> bitXorProgram = {
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      plain(0x07, "7"),
      plain(0x39, "К ⊕"),
      plain(0x0e, "В↑"),
      plain(0x08, "8"),
      plain(0x0a, "."),
      plain(0x06, "6"),
      halt(),
    };
    const std::vector<IrOp> bitNotProgram = {
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x3a, "К ИНВ"),
      plain(0x0e, "В↑"),
      plain(0x08, "8"),
      plain(0x0a, "."),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      plain(0x06, "6"),
      halt(),
    };
    const auto bitXorResult = run(bitXorProgram);
    const auto bitNotResult = run(bitNotProgram);

    check_applied(bitXorResult.applied, 2, "x2-literal-restore uses concrete MK-61 bitwise X2 facts");
    check_ops_equal(bitXorResult.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x06, "6"),
      plain(0x0e, "В↑"),
      plain(0x07, "7"),
      plain(0x39, "К ⊕"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 8.6 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete MK-61 bitwise X2 facts");
    check_applied(bitNotResult.applied, 8, "x2-literal-restore uses concrete MK-61 bitwise X2 facts");
    check_ops_equal(bitNotResult.ops, (std::vector<IrOp>{
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x09, "9"),
      plain(0x3a, "К ИНВ"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 8.6666666 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses concrete MK-61 bitwise X2 facts");
  }

  {
    // x2-literal-restore uses decimal-only structural bitwise X2 facts
    const std::vector<IrOp> program = {
      recall("1", "preload const 8A000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 85000000"),
      plain(0x37, "К ∧"),
      plain(0x0e, "В↑"),
      plain(0x08, "8"),
      plain(0x0a, "."),
      plain(0x00, "0"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 2, "x2-literal-restore uses decimal-only structural bitwise X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1", "preload const 8A000000"),
      plain(0x0e, "В↑"),
      recall("2", "preload const 85000000"),
      plain(0x37, "К ∧"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 8.0 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses decimal-only structural bitwise X2 facts");
  }

  {
    // x2-literal-restore uses exact MK-61 degree/minute conversion X2 facts
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      plain(0x33, "К °<-′"),
      plain(0x0e, "В↑"),
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x01, "1"),
      plain(0x05, "5"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 3, "x2-literal-restore uses exact MK-61 degree/minute conversion X2 facts");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x05, "5"),
      plain(0x33, "К °<-′"),
      plain(0x0e, "В↑"),
      lit_dot("restore literal 1.15 from hidden X2 temp"),
      halt(),
    }), "x2-literal-restore uses exact MK-61 degree/minute conversion X2 facts");
  }

  {
    // x2-literal-restore keeps a repeated literal before a branch target X2 restore
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x20, "Fπ"),
      plain(0x20, "Fπ"),
      plain(0x01, "1"),
      plain(0x02, "2"),
      cjump("restore"),
      halt(),
      label("restore"),
      plain(0x0c, "ВП"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "x2-literal-restore keeps a repeated literal before a branch target X2 restore");
    check_ops_equal(result.ops, program, "x2-literal-restore keeps a repeated literal before a branch target X2 restore");
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
  require(message.empty(), "x2-literal-restore parity divergence set changed:" + message);
}

} // namespace mkpro::tests
