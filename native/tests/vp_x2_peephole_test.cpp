#include "mkpro/core/passes/vp_x2_peephole.hpp"

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (vp-x2-peephole describe block; 51 it(...)
//   blocks). Every vpX2Peephole.run(...) assertion is ported here 1:1. The
//   native pass is a faithful port of vp-x2-peephole.ts, so full parity is
//   expected. Failures are collected and checked against an (initially empty)
//   deferred set using the divergence-ratchet pattern.

void vp_x2_peephole_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};

  std::vector<std::string> failures;
  const auto fail = [&](const std::string& label) { failures.push_back(label); };
  const auto run = [&](const std::vector<IrOp>& p) {
    return core::passes::vp_x2_peephole_pass().run(p, ctx);
  };
  const auto role_plain = [](int opcode, std::string mnemonic) {
    IrOp op = plain(opcode, std::move(mnemonic));
    op.meta.roles = {"display-byte"};
    return op;
  };
  const auto plain_raw = [](int opcode, std::string mnemonic) {
    IrOp op = plain(opcode, std::move(mnemonic));
    op.meta.raw = true;
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
  const auto check_no_plain_opcode = [&](const std::vector<IrOp>& ops, int opcode,
                                         const std::string& label) {
    for (const IrOp& op : ops)
      if (op.kind == IrKind::Plain && op.opcode == opcode) {
        fail(label);
        return;
      }
  };
  const auto check_detail_contains = [&](const mkpro::core::passes::PassResult& result,
                                         const std::string& needle, const std::string& label) {
    if (result.optimizations.empty() ||
        result.optimizations.front().detail.find(needle) == std::string::npos)
      fail(label);
  };

  {
    // vp-x2-peephole removes fractional op already supplied by a display ВП boundary
    const std::vector<IrOp> program = {
      plain_with_comment(0x0c, "ВП", "display X2 boundary"),
      plain_with_comment(0x35, "К {x}", "display frac"),
      halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "vp-x2-peephole removes fractional op already supplied by a display ВП boundary");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes fractional op already supplied by a display ВП boundary");
  }

  {
    // vp-x2-peephole removes fractional op already supplied by an ordinary X2 ВП boundary
    const std::vector<IrOp> program = {
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_with_comment(0x35, "К {x}", "frac after X2 restore"),
      halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 1, "vp-x2-peephole removes fractional op already supplied by an ordinary X2 ВП boundary");
    check_detail_contains(result, "ВП/X2 boundary", "vp-x2-peephole removes fractional op already supplied by an ordinary X2 ВП boundary");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes fractional op already supplied by an ordinary X2 ВП boundary");
  }

  {
    // vp-x2-peephole keeps К {x} after unmarked ordinary opcode context
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps К {x} after unmarked ordinary opcode context");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps К {x} after unmarked ordinary opcode context");
  }

  {
    // vp-x2-peephole removes unmarked К {x} after a proved ВП/X2 boundary
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes unmarked К {x} after a proved ВП/X2 boundary");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      halt(),
    }), "vp-x2-peephole removes unmarked К {x} after a proved ВП/X2 boundary");
  }

  {
    // vp-x2-peephole removes К {x} after a proved ВП/X2 boundary through empty ops
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes К {x} after a proved ВП/X2 boundary through empty ops");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain(0x54, "КНОП"),
      plain(0x55, "К1"),
      halt(),
    }), "vp-x2-peephole removes К {x} after a proved ВП/X2 boundary through empty ops");
  }

  {
    // vp-x2-peephole keeps К {x} after a role-bearing empty op
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      role_plain(0x55, "К1"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps К {x} after a role-bearing empty op");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps К {x} after a role-bearing empty op");
  }

  {
    // vp-x2-peephole removes К {x} after a proved boundary through marker labels
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes К {x} after a proved boundary through marker labels");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain(0x54, "КНОП"),
      label("marker"),
      plain(0x55, "К1"),
      halt(),
    }), "vp-x2-peephole removes К {x} after a proved boundary through marker labels");
  }

  {
    // vp-x2-peephole removes К {x} after a proved boundary through X-preserving stores
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      store("2"),
      plain(0x0e, "В↑"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes К {x} after a proved boundary through X-preserving stores");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      store("2"),
      plain(0x0e, "В↑"),
      halt(),
    }), "vp-x2-peephole removes К {x} after a proved boundary through X-preserving stores");
  }

  {
    // vp-x2-peephole removes a no-op К {x} for a proved closed fractional X value
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a no-op К {x} for a proved closed fractional X value");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К {x} for a proved closed fractional X value");
  }

  {
    // vp-x2-peephole removes a no-op К {x} for an exact fractional display shape
    const std::vector<IrOp> program = {
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a no-op К {x} for an exact fractional display shape");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x05, "5"),
      plain(0x0c, "ВП"),
      plain(0x0b, "/-/"),
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К {x} for an exact fractional display shape");
  }

  {
    // vp-x2-peephole removes a no-op К [x] for an exact integer display shape
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a no-op К [x] for an exact integer display shape");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К [x] for an exact integer display shape");
  }

  {
    // vp-x2-peephole keeps К [x] when integer-part would normalize the display shape
    const std::vector<IrOp> leadingZero = {
      plain(0x00, "0"),
      plain(0x02, "2"),
      plain(0x34, "К [x]"),
      halt(),
    };
    const std::vector<IrOp> rawIntegerPart = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain_raw(0x34, "К [x]"),
      halt(),
    };

    check_applied(run(leadingZero).applied, 0, "vp-x2-peephole keeps К [x] when integer-part would normalize the display shape");
    check_applied(run(rawIntegerPart).applied, 0, "vp-x2-peephole keeps К [x] when integer-part would normalize the display shape");
  }

  {
    // vp-x2-peephole removes a no-op К [x] for exact closed exponent integer displays
    const std::vector<IrOp> positive = {
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    };
    const std::vector<IrOp> negative = {
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    };

    check_ops_equal(run(positive).ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К [x] for exact closed exponent integer displays");
    check_ops_equal(run(negative).ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      plain(0x0c, "ВП"),
      plain(0x08, "8"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К [x] for exact closed exponent integer displays");
  }

  {
    // vp-x2-peephole removes a no-op К [x] for exact closed structural exponent integer displays
    const std::vector<IrOp> integerExponent = {
      recall("1", "preload const 123E1"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    };
    const std::vector<IrOp> shiftedFraction = {
      recall("1", "preload const 1.23E2"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    };
    const std::vector<IrOp> fractionalExponent = {
      recall("1", "preload const 1.23E-1"),
      plain(0xf0, "F* empty F0"),
      plain(0x34, "К [x]"),
      halt(),
    };

    check_ops_equal(run(integerExponent).ops, (std::vector<IrOp>{
      recall("1", "preload const 123E1"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К [x] for exact closed structural exponent integer displays");
    check_ops_equal(run(shiftedFraction).ops, (std::vector<IrOp>{
      recall("1", "preload const 1.23E2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К [x] for exact closed structural exponent integer displays");
    check_applied(run(fractionalExponent).applied, 0, "vp-x2-peephole removes a no-op К [x] for exact closed structural exponent integer displays");
  }

  {
    // vp-x2-peephole removes a no-op К |x| for an exact non-negative integer display shape
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a no-op К |x| for an exact non-negative integer display shape");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К |x| for an exact non-negative integer display shape");
  }

  {
    // vp-x2-peephole removes a no-op К |x| for an exact non-negative scientific display shape
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x13, "/"),
      plain(0x31, "К |x|"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a no-op К |x| for an exact non-negative scientific display shape");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0e, "В↑"),
      plain(0x02, "2"),
      plain(0x13, "/"),
      halt(),
    }), "vp-x2-peephole removes a no-op К |x| for an exact non-negative scientific display shape");
  }

  {
    // vp-x2-peephole removes a no-op К |x| in closed VP display context
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      plain(0x31, "К |x|"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a no-op К |x| in closed VP display context");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x02, "2"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      plain(0x20, "Fπ"),
      halt(),
    }), "vp-x2-peephole removes a no-op К |x| in closed VP display context");
  }

  {
    // vp-x2-peephole keeps К |x| for negative or raw integer display shapes
    const std::vector<IrOp> negative = {
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x31, "К |x|"),
      halt(),
    };
    const std::vector<IrOp> rawAbs = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain_raw(0x31, "К |x|"),
      halt(),
    };

    check_applied(run(negative).applied, 0, "vp-x2-peephole keeps К |x| for negative or raw integer display shapes");
    check_applied(run(rawAbs).applied, 0, "vp-x2-peephole keeps К |x| for negative or raw integer display shapes");
  }

  {
    // vp-x2-peephole removes a no-op К ЗН for exact sign-normal display values
    const std::vector<IrOp> positive = {
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      halt(),
    };
    const std::vector<IrOp> negative = {
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      halt(),
    };
    const std::vector<IrOp> zero = {
      plain(0x00, "0"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      halt(),
    };
    const std::vector<IrOp> nonNoop = {
      plain(0x02, "2"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      halt(),
    };

    check_ops_equal(run(positive).ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К ЗН for exact sign-normal display values");
    check_ops_equal(run(negative).ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К ЗН for exact sign-normal display values");
    check_ops_equal(run(zero).ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a no-op К ЗН for exact sign-normal display values");
    check_applied(run(nonNoop).applied, 0, "vp-x2-peephole removes a no-op К ЗН for exact sign-normal display values");
  }

  {
    // vp-x2-peephole keeps no-op К ЗН when it shields a following dot restore
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps no-op К ЗН when it shields a following dot restore");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps no-op К ЗН when it shields a following dot restore");
  }

  {
    // vp-x2-peephole keeps no-op-looking К {x} during active fractional entry
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0x35, "К {x}"),
      plain(0x06, "6"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps no-op-looking К {x} during active fractional entry");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps no-op-looking К {x} during active fractional entry");
  }

  {
    // vp-x2-peephole keeps a fractional no-op К {x} when its X2 sync reaches dot
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps a fractional no-op К {x} when its X2 sync reaches dot");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps a fractional no-op К {x} when its X2 sync reaches dot");
  }

  {
    // vp-x2-peephole removes a fractional no-op К {x} before dot when X2 already has the same value
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a fractional no-op К {x} before dot when X2 already has the same value");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    }), "vp-x2-peephole removes a fractional no-op К {x} before dot when X2 already has the same value");
  }

  {
    // vp-x2-peephole removes a negative fractional no-op К {x} after an X2 sync
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a negative fractional no-op К {x} after an X2 sync");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0x0a, "."),
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      halt(),
    }), "vp-x2-peephole removes a negative fractional no-op К {x} after an X2 sync");
  }

  {
    // vp-x2-peephole removes a repeated negative-integer fractional no-op after visible zero is proved
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a repeated negative-integer fractional no-op after visible zero is proved");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-x2-peephole removes a repeated negative-integer fractional no-op after visible zero is proved");
  }

  {
    // vp-x2-peephole removes a visible sign no-op before immediate ВП when the source is unchanged
    const std::vector<IrOp> program = {
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };

    check_ops_equal(run(program).ops, (std::vector<IrOp>{
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    }), "vp-x2-peephole removes a visible sign no-op before immediate ВП when the source is unchanged");
  }

  {
    // vp-x2-peephole keeps a visible fractional no-op before immediate ВП when the source is not proved unchanged
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps a visible fractional no-op before immediate ВП when the source is not proved unchanged");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps a visible fractional no-op before immediate ВП when the source is not proved unchanged");
  }

  {
    // vp-x2-peephole keeps a visible no-op before immediate ВП when a label can enter there
    const std::vector<IrOp> program = {
      jump("entry"),
      plain(0x01, "1"),
      plain(0xf0, "F* empty F0"),
      plain(0x32, "К ЗН"),
      label("entry"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps a visible no-op before immediate ВП when a label can enter there");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps a visible no-op before immediate ВП when a label can enter there");
  }

  {
    // vp-x2-peephole keeps the first negative-integer fractional op before its later signed-zero sync
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x0b, "/-/"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0xf0, "F* empty F0"),
      plain(0x0c, "ВП"),
      plain(0x03, "3"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps the first negative-integer fractional op before its later signed-zero sync");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps the first negative-integer fractional op before its later signed-zero sync");
  }

  {
    // vp-x2-peephole removes a fractional no-op К {x} before dot through a preserving gap even when X2 differs
    const std::vector<IrOp> program = {
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a fractional no-op К {x} before dot through a preserving gap even when X2 differs");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      plain(0x02, "2"),
      plain(0x35, "К {x}"),
      plain(0x54, "К НОП"),
      plain(0x0a, "."),
      halt(),
    }), "vp-x2-peephole removes a fractional no-op К {x} before dot through a preserving gap even when X2 differs");
  }

  {
    // vp-x2-peephole keeps an immediate fractional no-op К {x} dot boundary
    const std::vector<IrOp> program = {
      plain(0x00, "0"),
      plain(0x0a, "."),
      plain(0x05, "5"),
      plain(0xf0, "F* empty F0"),
      plain(0x35, "К {x}"),
      plain(0x0a, "."),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps an immediate fractional no-op К {x} dot boundary");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps an immediate fractional no-op К {x} dot boundary");
  }

  {
    // vp-x2-peephole removes К {x} after a proved boundary through transparent direct-return helpers
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      call("noop"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes К {x} after a proved boundary through transparent direct-return helpers");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      call("noop"),
      halt(),
    }), "vp-x2-peephole removes К {x} after a proved boundary through transparent direct-return helpers");
  }

  {
    // vp-x2-peephole removes К {x} after a proved boundary through nested transparent return helpers
    const std::vector<IrOp> program = {
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      call("outer"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes К {x} after a proved boundary through nested transparent return helpers");
    check_ops_equal(result.ops, (std::vector<IrOp>{
      jump("main"),
      label("noop"),
      store("2"),
      plain(0x54, "КНОП"),
      ret(),
      label("outer"),
      call("noop"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      call("outer"),
      halt(),
    }), "vp-x2-peephole removes К {x} after a proved boundary through nested transparent return helpers");
  }

  {
    // vp-x2-peephole keeps К {x} after direct-return helpers that change X
    const std::vector<IrOp> program = {
      jump("main"),
      label("load"),
      plain(0x20, "F pi"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      call("load"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps К {x} after direct-return helpers that change X");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps К {x} after direct-return helpers that change X");
  }

  {
    // vp-x2-peephole keeps К {x} after nested return helpers that change X
    const std::vector<IrOp> program = {
      jump("main"),
      label("load"),
      plain(0x20, "F pi"),
      ret(),
      label("outer"),
      call("load"),
      ret(),
      label("main"),
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      call("outer"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps К {x} after nested return helpers that change X");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps К {x} after nested return helpers that change X");
  }

  {
    // vp-x2-peephole keeps К {x} across role-bearing X-preserving gaps
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      role_plain(0x0e, "В↑"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps К {x} across role-bearing X-preserving gaps");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps К {x} across role-bearing X-preserving gaps");
  }

  {
    // vp-x2-peephole keeps К {x} across referenced labels
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      jump("entry"),
      label("entry"),
      plain(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps К {x} across referenced labels");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps К {x} across referenced labels");
  }

  {
    // vp-x2-peephole keeps raw К {x} after a proved ВП/X2 boundary
    const std::vector<IrOp> program = {
      recall("1"),
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_raw(0x35, "К {x}"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps raw К {x} after a proved ВП/X2 boundary");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps raw К {x} after a proved ВП/X2 boundary");
  }

  {
    // vp-x2-peephole removes a marked ВП/X2 boundary reached by a direct conditional jump edge
    const std::vector<IrOp> program = {
      recall("1"),
      cjump("target"),
      jump("end"),
      label("target"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      label("end"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a direct conditional jump edge");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a direct conditional jump edge");
  }

  {
    // vp-x2-peephole removes a marked ВП/X2 boundary reached by a counted-loop jump edge
    const std::vector<IrOp> program = {
      recall("1"),
      loop("target"),
      jump("end"),
      label("target"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      label("end"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a counted-loop jump edge");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a counted-loop jump edge");
  }

  {
    // vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect jump
    const std::vector<IrOp> program = {
      recall("1"),
      known_target_indirect_jump("8", 3),
      halt(),
      label("target"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect jump");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect jump");
  }

  {
    // vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect conditional jump edge
    const std::vector<IrOp> program = {
      recall("1"),
      known_target_indirect_cjump("8", 4),
      jump("end"),
      label("target"),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      halt(),
      label("end"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect conditional jump edge");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes a marked ВП/X2 boundary reached by a proved stable indirect conditional jump edge");
  }

  {
    // vp-x2-peephole keeps fraction after an unmarked direct conditional fallthrough X2 sync
    const std::vector<IrOp> program = {
      recall("1"),
      cjump("target"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      label("target"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps fraction after an unmarked direct conditional fallthrough X2 sync");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps fraction after an unmarked direct conditional fallthrough X2 sync");
  }

  {
    // vp-x2-peephole keeps fraction after an unmarked counted-loop fallthrough X2 sync
    const std::vector<IrOp> program = {
      recall("1"),
      loop("target"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      label("target"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps fraction after an unmarked counted-loop fallthrough X2 sync");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps fraction after an unmarked counted-loop fallthrough X2 sync");
  }

  {
    // vp-x2-peephole removes fraction after a marked indirect conditional fallthrough boundary
    const std::vector<IrOp> program = {
      recall("1"),
      known_target_indirect_cjump("8", 6),
      plain_with_comment(0x0c, "ВП", "ordinary X2 restore boundary"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      jump("end"),
      label("target"),
      halt(),
      label("end"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 1, "vp-x2-peephole removes fraction after a marked indirect conditional fallthrough boundary");
    check_no_plain_opcode(result.ops, 0x35, "vp-x2-peephole removes fraction after a marked indirect conditional fallthrough boundary");
  }

  {
    // vp-x2-peephole keeps an unmarked ВП at a CFG join
    const std::vector<IrOp> program = {
      recall("1"),
      cjump("target"),
      label("join"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      jump("end"),
      label("target"),
      jump("join"),
      label("end"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps an unmarked ВП at a CFG join");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps an unmarked ВП at a CFG join");
  }

  {
    // vp-x2-peephole keeps fraction after immediate ВП context
    const std::vector<IrOp> program = {
      recall("1"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps fraction after immediate ВП context");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps fraction after immediate ВП context");
  }

  {
    // vp-x2-peephole keeps fraction after ВП without a proved X2 source
    const std::vector<IrOp> program = {
      plain(0x20, "F pi"),
      plain_with_comment(0x0c, "ВП", "mantissa splice"),
      plain_with_comment(0x35, "К {x}", "frac after restore"),
      halt(),
    };
    const auto result = run(program);

    check_applied(result.applied, 0, "vp-x2-peephole keeps fraction after ВП without a proved X2 source");
    check_ops_equal(result.ops, program, "vp-x2-peephole keeps fraction after ВП without a proved X2 source");
  }

  {
    // vp-x2-peephole refuses ordinary exponent ВП boundaries
    const std::vector<IrOp> program = {
      plain_with_comment(0x0c, "ВП", "exponent entry"),
      plain_with_comment(0x35, "К {x}", "frac"),
      halt(),
    };
    const auto result = run(program);
    check_applied(result.applied, 0, "vp-x2-peephole refuses ordinary exponent ВП boundaries");
  }

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
  require(message.empty(), "vp-x2-peephole parity divergence set changed:" + message);
}

} // namespace mkpro::tests
