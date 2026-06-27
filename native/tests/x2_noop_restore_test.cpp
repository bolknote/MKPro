#include "mkpro/core/passes/x2_noop_restore.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

// Traceability:
// - tests/compiler/passes.test.ts (x2-noop-restore assertions, scattered across
//   the large pass describe block at lines ~15299 and ~18245-20922).
//
// Every x2NoopRestore.run(...) assertion in passes.test.ts is ported here
// 1:1. The native pass consumes the faithful compute_x2_noop_restore_removed
// decision logic in helpers.cpp, so full parity is expected. Failures are
// collected and checked against an (initially empty) deferred set using the
// divergence-ratchet pattern: any unexpected divergence fails immediately, and
// any deferred case that starts passing forces promotion to fully covered.

void x2_noop_restore_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;
  const mkpro::CompileOptions options = noop_options();
  const core::passes::PassContext ctx{.options = options};
  const auto run = [&](const std::vector<IrOp>& program) {
    return core::passes::x2_noop_restore_pass().run(program, ctx);
  };

  std::vector<std::string> failures;
  const auto check_applied = [&](int actual, int expected, const std::string& label) {
    if (actual != expected)
      failures.push_back(label);
  };
  const auto check_ops_equal = [&](const std::vector<IrOp>& a, const std::vector<IrOp>& e,
                                   const std::string& label) {
    if (mkpro::ir_ops_to_json(a) != mkpro::ir_ops_to_json(e))
      failures.push_back(label);
  };

  const auto plain_role = [](int opcode, const std::string& mnemonic) {
    IrOp op = plain(opcode, mnemonic);
    op.meta.roles = {"display-byte"};
    return op;
  };

  {
    const std::vector<IrOp> program = {recall("1"), store("2"), plain(0x54, "К НОП"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes a safe dot when X already has the X2 register value");
    check_ops_equal(result.ops, {recall("1"), store("2"), plain(0x54, "К НОП"), halt()},
                    "removes a safe dot when X already has the X2 register value: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"),          plain(0x21, "F sqrt"),
                                       plain(0xf0, "F* empty F0"), plain(0x54, "К НОП"),
                                       plain(0x55, "К 1"),         plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "removes dot after a synced pure unary expr across a free-standing gap");
    check_ops_equal(result.ops,
                    {plain(0x02, "2"), plain(0x21, "F sqrt"), plain(0xf0, "F* empty F0"),
                     plain(0x54, "К НОП"), plain(0x55, "К 1"), halt()},
                    "removes dot after a synced pure unary expr across a free-standing gap: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"),   plain(0x21, "F sqrt"),
                                       plain(0x54, "К НОП"), plain(0x55, "К 1"),
                                       plain(0x0a, "."),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 0,
                  "keeps dot after a pure unary expr without an explicit result sync");
    check_ops_equal(result.ops, program,
                    "keeps dot after a pure unary expr without an explicit result sync: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot when it is a decimal separator");
    check_ops_equal(result.ops, program, "keeps dot when it is a decimal separator: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot immediately after a recall X2 sync");
    check_ops_equal(result.ops, {recall("1"), halt()},
                    "removes dot immediately after a recall X2 sync: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"), store("1"),   store("2"),
                                       recall("1"),      recall("2"),  plain(0x14, "X↔Y"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "uses register memory when X and X2 are different aliases of the same value");
    check_ops_equal(
        result.ops,
        {plain(0x02, "2"), store("1"), store("2"), recall("1"), recall("2"), plain(0x14, "X↔Y"),
         halt()},
        "uses register memory when X and X2 are different aliases of the same value: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x0d, "Cx"), plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot immediately after Cx zero sync");
    check_ops_equal(result.ops, {plain(0x0d, "Cx"), halt()},
                    "removes dot immediately after Cx zero sync: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"), cjump("done"), plain(0x0a, "."),
                                       halt(),      label("done"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses path-sensitive conditional fallthrough X2 sync");
    check_ops_equal(result.ops, {recall("1"), cjump("done"), halt(), label("done"), halt()},
                    "uses path-sensitive conditional fallthrough X2 sync: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"),    plain(0x0a, "."), cjump("done"),
                                       halt(),         label("done"),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before a branch with no reachable X2 restore");
    check_ops_equal(result.ops, {recall("1"), cjump("done"), halt(), label("done"), halt()},
                    "removes dot before a branch with no reachable X2 restore: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"),       plain(0x0a, "."), cjump("restore"),
                                       halt(),            label("restore"), plain(0x0c, "ВП"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot before a branch target X2 restore");
    check_ops_equal(result.ops, program, "keeps dot before a branch target X2 restore: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"), loop("done"),  plain(0x0a, "."),
                                       halt(),      label("done"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "uses path-sensitive loop fallthrough X2 sync");
    check_ops_equal(result.ops, {recall("1"), loop("done"), halt(), label("done"), halt()},
                    "uses path-sensitive loop fallthrough X2 sync: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       known_target_indirect_cjump("7", 5),
                                       plain(0x0a, "."),
                                       halt(),
                                       label("done"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "removes dot after proved indirect conditional preserved normalized X2");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x02, "2"), known_target_indirect_cjump("7", 5),
                     halt(), label("done"), halt()},
                    "removes dot after proved indirect conditional preserved normalized X2: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),
                                       plain(0x02, "2"),
                                       known_target_indirect_cjump("7", 5),
                                       plain(0x0a, "."),
                                       halt(),
                                       label("done"),
                                       halt()};
    const auto result = run(program);
    check_applied(
        result.applied, 1,
        "removes dot after proved indirect conditional preserved visible leading-zero X2");
    check_ops_equal(
        result.ops,
        {plain(0x00, "0"), plain(0x02, "2"), known_target_indirect_cjump("7", 5), halt(),
         label("done"), halt()},
        "removes dot after proved indirect conditional preserved visible leading-zero X2: ops");
  }

  {
    const std::vector<IrOp> program = {label("main"), call("load"),  plain(0x0a, "."),
                                       halt(),        label("load"), recall("1"),
                                       ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot immediately after direct return X2 sync");
    check_ops_equal(
        result.ops,
        {label("main"), call("load"), halt(), label("load"), recall("1"), ret()},
        "removes dot immediately after direct return X2 sync: ops");
  }

  {
    const std::vector<IrOp> program = {label("main"),
                                       known_target_indirect_call("7", 3),
                                       plain(0x0a, "."),
                                       halt(),
                                       label("load"),
                                       recall("1"),
                                       ret()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot immediately after known indirect return X2 sync");
    check_ops_equal(result.ops,
                    {label("main"), known_target_indirect_call("7", 3), halt(), label("load"),
                     recall("1"), ret()},
                    "removes dot immediately after known indirect return X2 sync: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x0d, "Cx"), orphan_address(54), orphan_address(55),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes a safe dot for a proved zero value, not only registers");
    check_ops_equal(result.ops, {plain(0x0d, "Cx"), orphan_address(54), orphan_address(55), halt()},
                    "removes a safe dot for a proved zero value, not only registers: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), store("2"),
                                       store("3"),       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after a proved normalized decimal literal");
    check_ops_equal(result.ops, {plain(0x01, "1"), plain(0x02, "2"), store("2"), store("3"), halt()},
                    "removes dot after a proved normalized decimal literal: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), plain(0x0b, "/-/"),
                                       store("2"),       store("3"),       plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after a proved normalized signed decimal literal");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x02, "2"), plain(0x0b, "/-/"), store("2"), store("3"),
                     halt()},
                    "removes dot after a proved normalized signed decimal literal: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x0c, "ВП"), plain(0x08, "8"),
                                       store("2"),       store("3"),         plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after a proved decimal exponent display shape");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x0c, "ВП"), plain(0x08, "8"), store("2"), store("3"),
                     halt()},
                    "removes dot after a proved decimal exponent display shape: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x05, "5"), plain(0x0c, "ВП"), plain(0x03, "3"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot in active exponent-entry context");
    check_ops_equal(result.ops, program, "keeps dot in active exponent-entry context: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"), plain(0x02, "2"), store("2"),
                                       store("3"),       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after a visible-equivalent leading-zero X2 literal");
    check_ops_equal(result.ops, {plain(0x00, "0"), plain(0x02, "2"), store("2"), store("3"), halt()},
                    "removes dot after a visible-equivalent leading-zero X2 literal: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after F* syncs a leading-zero literal");
    check_ops_equal(result.ops, {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                                 halt()},
                    "removes dot after F* syncs a leading-zero literal: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"), plain(0x02, "2"), plain(0x0e, "В↑"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after stack lift syncs a leading-zero literal");
    check_ops_equal(result.ops, {plain(0x00, "0"), plain(0x02, "2"), plain(0x0e, "В↑"), halt()},
                    "removes dot after stack lift syncs a leading-zero literal: ops");
  }

  {
    const std::vector<IrOp> square_program = {recall("1", "preload const A"), plain(0x22, "F x^2"),
                                              plain(0xf0, "F* empty F0"), plain(0x0a, "."), halt()};
    const std::vector<IrOp> multiply_program = {recall("1", "preload const A"),
                                                plain(0x0e, "В↑"),
                                                plain(0x01, "1"),
                                                plain(0x08, "8"),
                                                plain(0x12, "×"),
                                                plain(0xf0, "F* empty F0"),
                                                plain(0x0a, "."),
                                                halt()};
    const auto square_result = run(square_program);
    const auto multiply_result = run(multiply_program);
    check_applied(square_result.applied, 1,
                  "removes dot after structural arithmetic is normalized by X2 sync (square)");
    check_ops_equal(square_result.ops,
                    {recall("1", "preload const A"), plain(0x22, "F x^2"), plain(0xf0, "F* empty F0"),
                     halt()},
                    "removes dot after structural arithmetic is normalized by X2 sync (square): ops");
    check_applied(multiply_result.applied, 1,
                  "removes dot after structural arithmetic is normalized by X2 sync (multiply)");
    check_ops_equal(
        multiply_result.ops,
        {recall("1", "preload const A"), plain(0x0e, "В↑"), plain(0x01, "1"), plain(0x08, "8"),
         plain(0x12, "×"), plain(0xf0, "F* empty F0"), halt()},
        "removes dot after structural arithmetic is normalized by X2 sync (multiply): ops");
  }

  {
    const std::vector<IrOp> conditional_program = {recall("1", "preload const A"),
                                                   plain(0x22, "F x^2"),
                                                   cjump("done"),
                                                   plain(0x0a, "."),
                                                   halt(),
                                                   label("done"),
                                                   halt()};
    const std::vector<IrOp> loop_program = {recall("1", "preload const A"),
                                            plain(0x22, "F x^2"),
                                            loop("done"),
                                            plain(0x0a, "."),
                                            halt(),
                                            label("done"),
                                            halt()};
    const std::vector<IrOp> return_program = {label("main"),
                                              call("load"),
                                              plain(0x0a, "."),
                                              halt(),
                                              label("load"),
                                              recall("1", "preload const A"),
                                              plain(0x22, "F x^2"),
                                              ret()};
    check_ops_equal(run(conditional_program).ops,
                    {recall("1", "preload const A"), plain(0x22, "F x^2"), cjump("done"), halt(),
                     label("done"), halt()},
                    "removes dot after path-sensitive structural arithmetic syncs (conditional)");
    check_ops_equal(run(loop_program).ops,
                    {recall("1", "preload const A"), plain(0x22, "F x^2"), loop("done"), halt(),
                     label("done"), halt()},
                    "removes dot after path-sensitive structural arithmetic syncs (loop)");
    check_ops_equal(run(return_program).ops,
                    {label("main"), call("load"), halt(), label("load"),
                     recall("1", "preload const A"), plain(0x22, "F x^2"), ret()},
                    "removes dot after path-sensitive structural arithmetic syncs (return)");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                                       plain(0x0b, "/-/"), plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot immediately after modeled closed sign-change");
    check_ops_equal(result.ops,
                    {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x0b, "/-/"), halt()},
                    "removes dot immediately after modeled closed sign-change: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"),
                                       plain(0x54, "К НОП"), plain(0x55, "К 1"),
                                       plain(0x0a, "."),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after modeled closed sign-change through empty ops");
    check_ops_equal(result.ops,
                    {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x0b, "/-/"), plain(0x54, "К НОП"), plain(0x55, "К 1"), halt()},
                    "removes dot after modeled closed sign-change through empty ops: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"),
                                       orphan_address(54),  plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "removes dot after modeled closed sign-change through address gaps");
    check_ops_equal(result.ops,
                    {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x0b, "/-/"), orphan_address(54), halt()},
                    "removes dot after modeled closed sign-change through address gaps: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),
                                       plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"),
                                       plain(0x0b, "/-/"),
                                       plain_role(0x54, "К НОП"),
                                       plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0,
                  "keeps dot after modeled sign-change through role-bearing empty ops");
    check_ops_equal(result.ops, program,
                    "keeps dot after modeled sign-change through role-bearing empty ops: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x35, "К {x}"), plain(0x0e, "В↑"), plain(0x0b, "/-/"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after modeled opaque closed sign-change");
    check_ops_equal(result.ops, {plain(0x35, "К {x}"), plain(0x0e, "В↑"), plain(0x0b, "/-/"), halt()},
                    "removes dot after modeled opaque closed sign-change: ops");
  }

  {
    const std::vector<IrOp> program = {recall("2", "preload const 8.70Е2-6С"), plain(0x0b, "/-/"),
                                       plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot after modeled structural closed sign-change");
    check_ops_equal(result.ops, program, "keeps dot after modeled structural closed sign-change: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const A"), plain(0x0b, "/-/"),
                                       plain(0x0b, "/-/"), plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after an emulator-pinned structural sign pair");
    check_ops_equal(result.ops,
                    {recall("1", "preload const A"), plain(0x0b, "/-/"), plain(0x0b, "/-/"), halt()},
                    "removes dot after an emulator-pinned structural sign pair: ops");
  }

  {
    const std::vector<IrOp> d_program = {recall("1", "preload const D"), plain(0x0b, "/-/"),
                                         plain(0x0b, "/-/"), plain(0x0a, "."), halt()};
    const std::vector<IrOp> f_program = {recall("1", "preload const F"), plain(0x0b, "/-/"),
                                         plain(0x0b, "/-/"), plain(0x0a, "."), halt()};
    check_ops_equal(run(d_program).ops, d_program, "keeps unsafe structural sign-pair dots (D)");
    check_ops_equal(run(f_program).ops, f_program, "keeps unsafe structural sign-pair dots (F)");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const A"), plain(0x0b, "/-/"),
                                       plain(0x0b, "/-/"), plain(0x0a, "."), plain(0x0c, "ВП"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps structural sign-pair dot before observable VP context");
    check_ops_equal(result.ops, program,
                    "keeps structural sign-pair dot before observable VP context: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x05, "5"),   plain(0x0c, "ВП"),
                                       plain(0x03, "3"),    plain(0x0b, "/-/"),
                                       plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"),
                                       plain(0x0a, "."),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after modeled fractional closed sign-change");
    check_ops_equal(result.ops,
                    {plain(0x05, "5"), plain(0x0c, "ВП"), plain(0x03, "3"), plain(0x0b, "/-/"),
                     plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"), halt()},
                    "removes dot after modeled fractional closed sign-change: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x0a, "."), plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0a, "."), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after a synced fractional literal");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x0a, "."), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     halt()},
                    "removes dot after a synced fractional literal: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),   plain(0x0a, "."),
                                       plain(0x02, "2"),    plain(0xf0, "F* empty F0"),
                                       plain(0x0c, "ВП"),  plain(0x03, "3"),
                                       plain(0xf0, "F* empty F0"), plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot after a synced fractional exponent-entry");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x0a, "."), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x0c, "ВП"), plain(0x03, "3"), plain(0xf0, "F* empty F0"), halt()},
                    "removes dot after a synced fractional exponent-entry: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"),
                                       plain(0x0a, "."),    plain(0x0c, "ВП"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0,
                  "keeps dot after closed sign-change when it shapes a following VP");
    check_ops_equal(result.ops, program,
                    "keeps dot after closed sign-change when it shapes a following VP: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"),
                                       plain(0x54, "К НОП"), plain(0x0a, "."),
                                       plain(0x0c, "ВП"),  halt()};
    const auto result = run(program);
    check_applied(result.applied, 0,
                  "keeps dot after empty-op closed sign-change when it shapes a following VP");
    check_ops_equal(result.ops, program,
                    "keeps dot after empty-op closed sign-change when it shapes a following VP: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0b, "/-/"),
                                       orphan_address(54),  plain(0x54, "К НОП"),
                                       plain(0x0a, "."),    plain(0x0c, "ВП"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0,
                  "keeps dot after address-gap closed sign-change when it shapes a following VP");
    check_ops_equal(
        result.ops, program,
        "keeps dot after address-gap closed sign-change when it shapes a following VP: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"), plain(0x02, "2"), plain(0x0b, "/-/"),
                                       store("2"),       store("3"),       plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "removes dot after a visible-equivalent signed leading-zero X2 literal");
    check_ops_equal(result.ops,
                    {plain(0x00, "0"), plain(0x02, "2"), plain(0x0b, "/-/"), store("2"), store("3"),
                     halt()},
                    "removes dot after a visible-equivalent signed leading-zero X2 literal: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x0d, "Cx"), plain(0x54, "К НОП"), plain(0x55, "К 1"),
                                       plain(0x0a, "."), plain(0x0c, "ВП"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot when it would change the next VP context");
    check_ops_equal(result.ops, program, "keeps dot when it would change the next VP context: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0a, "."),
                                       plain(0x55, "К 1"),  plain(0x0c, "ВП"),
                                       plain(0x03, "3"),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before empty-op VP when the VP source is unchanged");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x55, "К 1"), plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
                    "removes dot before empty-op VP when the VP source is unchanged: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0a, "."),
                                       plain(0x0c, "ВП"),  plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before immediate VP when the VP source is unchanged");
    check_ops_equal(result.ops,
                    {plain(0x01, "1"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
                    "removes dot before immediate VP when the VP source is unchanged: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"), plain(0x02, "2"), plain(0x35, "К {x}"),
                                       plain(0x0a, "."), plain(0x0a, "."), plain(0x0b, "/-/"),
                                       plain(0x0c, "ВП"), plain(0x03, "3"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes repeated raw decimal dot before sign-gap VP");
    check_ops_equal(result.ops,
                    {plain(0x00, "0"), plain(0x02, "2"), plain(0x35, "К {x}"), plain(0x0a, "."),
                     plain(0x0b, "/-/"), plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
                    "removes repeated raw decimal dot before sign-gap VP: ops");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "КНОП"),
                                       ret(),
                                       label("main"),
                                       plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"),
                                       plain(0x0a, "."),
                                       call("transparent"),
                                       plain(0x0c, "ВП"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before transparent return helpers and immediate VP");
    check_ops_equal(result.ops,
                    {jump("main"), label("transparent"), plain(0x54, "КНОП"), ret(), label("main"),
                     plain(0x01, "1"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     call("transparent"), plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
                    "removes dot before transparent return helpers and immediate VP: ops");
  }

  {
    const std::vector<IrOp> program = {jump("main"),
                                       label("transparent"),
                                       plain(0x54, "КНОП"),
                                       ret(),
                                       label("main"),
                                       plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"),
                                       plain(0x0a, "."),
                                       plain(0x55, "К 1"),
                                       call("transparent"),
                                       plain(0x0c, "ВП"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before transparent return helpers and empty-op VP");
    check_ops_equal(result.ops,
                    {jump("main"), label("transparent"), plain(0x54, "КНОП"), ret(), label("main"),
                     plain(0x01, "1"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x55, "К 1"), call("transparent"), plain(0x0c, "ВП"), plain(0x03, "3"),
                     halt()},
                    "removes dot before transparent return helpers and empty-op VP: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0a, "."),
                                       label("marker"),     plain(0x55, "К 1"),
                                       plain(0x0c, "ВП"),  plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1,
                  "removes dot before marker labels and empty-op VP when the VP source is unchanged");
    check_ops_equal(
        result.ops,
        {plain(0x01, "1"), plain(0x02, "2"), plain(0xf0, "F* empty F0"), label("marker"),
         plain(0x55, "К 1"), plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
        "removes dot before marker labels and empty-op VP when the VP source is unchanged: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x00, "0"),   plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"), plain(0x0a, "."),
                                       plain(0x0b, "/-/"),  plain(0x54, "К НОП"),
                                       plain(0x0b, "/-/"),  plain(0x0c, "ВП"),
                                       plain(0x03, "3"),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before a proved restore-run VP source");
    check_ops_equal(result.ops,
                    {plain(0x00, "0"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
                     plain(0x0b, "/-/"), plain(0x54, "К НОП"), plain(0x0b, "/-/"), plain(0x0c, "ВП"),
                     plain(0x03, "3"), halt()},
                    "removes dot before a proved restore-run VP source: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"),   plain(0xf0, "F* empty F0"),
                                       store("1"),          plain(0x0a, "."),
                                       plain(0x0b, "/-/"),  plain(0x0c, "ВП"),
                                       plain(0x03, "3"),    halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before a store-backed sign VP source");
    check_ops_equal(result.ops,
                    {plain(0x02, "2"), plain(0xf0, "F* empty F0"), store("1"), plain(0x0b, "/-/"),
                     plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
                    "removes dot before a store-backed sign VP source: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x02, "2"),   plain(0xf0, "F* empty F0"),
                                       store("1"),          plain(0x0a, "."),
                                       plain(0x0b, "/-/"),  plain(0x0b, "/-/"),
                                       plain(0x0c, "ВП"),  plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot before a store-backed sign-pair VP source");
    check_ops_equal(result.ops,
                    {plain(0x02, "2"), plain(0xf0, "F* empty F0"), store("1"), plain(0x0b, "/-/"),
                     plain(0x0b, "/-/"), plain(0x0c, "ВП"), plain(0x03, "3"), halt()},
                    "removes dot before a store-backed sign-pair VP source: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"),
                                       plain(0x0a, "."),
                                       plain_role(0x55, "К 1"),
                                       plain(0x0c, "ВП"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot before role-bearing empty-op VP context");
    check_ops_equal(result.ops, program, "keeps dot before role-bearing empty-op VP context: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x01, "1"),
                                       plain(0x02, "2"),
                                       plain(0xf0, "F* empty F0"),
                                       plain(0x0a, "."),
                                       plain_role(0x0b, "/-/"),
                                       plain(0x0b, "/-/"),
                                       plain(0x0c, "ВП"),
                                       plain(0x03, "3"),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot before role-bearing sign-change VP context");
    check_ops_equal(result.ops, program, "keeps dot before role-bearing sign-change VP context: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x0a, "."), plain(0x0c, "ВП"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps immediate post-sync dot when it shapes the next VP context");
    check_ops_equal(result.ops, program,
                    "keeps immediate post-sync dot when it shapes the next VP context: ops");
  }

  {
    const std::vector<IrOp> program = {plain(0x0d, "Cx"), store("2"), plain(0x54, "К НОП"),
                                       plain(0x0a, "."), plain(0x0b, "/-/"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot when it would change the next sign-change context");
    check_ops_equal(result.ops, program,
                    "keeps dot when it would change the next sign-change context: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1"),       store("2"),
                                       plain(0x35, "К {x}"), plain(0x54, "К НОП"),
                                       plain(0x0a, "."),   halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps dot when X no longer has the hidden X2 value");
    check_ops_equal(result.ops, program, "keeps dot when X no longer has the hidden X2 value: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const C"), store("2"), plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 1, "removes dot-safe structural dots after a free-standing gap");
    check_ops_equal(result.ops, {recall("1", "preload const C"), store("2"), halt()},
                    "removes dot-safe structural dots after a free-standing gap: ops");
  }

  {
    const std::vector<IrOp> program = {recall("1", "preload const FACE"), store("2"), plain(0x0a, "."),
                                       halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps unsafe structural dots after a free-standing gap");
    check_ops_equal(result.ops, program,
                    "keeps unsafe structural dots after a free-standing gap: ops");
  }

  {
    // tests/compiler/passes.test.ts: "x2-literal-restore keeps its inserted
    // X2-sensitive dot explicit" — the noop pass must leave the literal-restore
    // dot (carrying an "X2" comment that is display-focus-sensitive) untouched.
    const std::vector<IrOp> program = {
        plain(0x01, "1"), plain(0x02, "2"), plain(0xf0, "F* empty F0"),
        plain_with_comment(0x0a, ".", "restore literal 12 from hidden X2 temp"), halt()};
    const auto result = run(program);
    check_applied(result.applied, 0, "keeps inserted X2-sensitive literal-restore dot explicit");
    check_ops_equal(result.ops, program,
                    "keeps inserted X2-sensitive literal-restore dot explicit: ops");
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
  require(message.empty(), "x2-noop-restore parity divergence set changed:" + message);
}

} // namespace mkpro::tests
