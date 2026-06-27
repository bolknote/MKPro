#include "mkpro/core/passes/helpers.hpp"

#include "ir_pass_test_support.hpp"
#include "test_support.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

// Faithful port of the program-level "x2 value dataflow" assertion group from
// tests/compiler/passes.test.ts. These drive compute_x2_value_states /
// transfer_x2_value_state_for_edge and assert on the observable state via the
// same serializers as the TS text helpers (x2ValueStateText, x2ShapeStateText,
// x2EntryStateText, x2VpContextStateText, x2StructuralVpContextStateText,
// x2VpEntryShapeText, x2VpEntryMantissaText, ...).
//
// Internal-predicate-only assertions (x2ShapeSetsHaveSameStructuralShape,
// analyzeX2VpShapeContext, the VP-source-proof / restore-gap scanner family)
// are NOT on the native public surface; where a TS case mixes those with
// observable state, the observable lines are ported and the internal-only line
// is noted in a comment.

namespace mkpro::tests {

namespace {

namespace P = mkpro::core::passes;
using P::X2ValueDataflowState;
using Set = std::set<std::string>;

std::string join_comma(const Set& s) {
  std::string out;
  bool first = true;
  for (const auto& e : s) {
    if (!first) out += ",";
    out += e;
    first = false;
  }
  return out;
}

std::string join_pipe(const Set& s) {
  std::string out;
  bool first = true;
  for (const auto& e : s) {
    if (!first) out += "|";
    out += e;
    first = false;
  }
  return out;
}

// x2ValueStateText / x2ShapeStateText for an always-present set.
std::string vt(const Set& s) { return join_comma(s); }

// x2ValueStateText / x2ShapeStateText for an optional set: undefined -> sentinel.
std::string vt(const std::optional<Set>& s) { return s.has_value() ? join_comma(*s) : "<undef>"; }

bool has(const Set& s, const std::string& fact) { return s.count(fact) > 0; }

std::string entry_text(const P::X2EntryState& e) {
  switch (e.kind) {
    case P::X2EntryState::Kind::Closed:
      return "closed";
    case P::X2EntryState::Kind::Unknown:
      return "unknown";
    case P::X2EntryState::Kind::Open:
      return "open:" + join_pipe(e.raw);
    case P::X2EntryState::Kind::Exponent:
      return "exponent:" + join_pipe(e.mantissa) + ":" + join_pipe(e.exponent);
  }
  return "?";
}

const X2ValueDataflowState& at(const std::vector<std::optional<X2ValueDataflowState>>& states,
                               size_t index) {
  require(index < states.size() && states[index].has_value(),
          "x2 value dataflow: missing state at index " + std::to_string(index));
  return *states[index];
}

void eq(const std::string& actual, const std::string& expected, const std::string& label) {
  require(actual == expected,
          "x2 value dataflow [" + label + "] expected\n  " + expected + "\ngot\n  " + actual);
}

void truthy(bool value, const std::string& label) {
  require(value, "x2 value dataflow [" + label + "] expected condition to hold");
}

std::vector<std::optional<X2ValueDataflowState>> compute(const std::vector<IrOp>& program,
                                                         bool track_register_memory = false) {
  P::X2ValueStatesOptions options;
  options.track_register_memory = track_register_memory;
  return P::compute_x2_value_states(program, options);
}

}  // namespace

void x2_value_dataflow_matches_typescript_contract() {
  using namespace mkpro::tests::irbuild;

  // x2 value dataflow tracks const zero through X-preserving gaps
  {
    const std::vector<IrOp> program = {plain(0x0d, "Cx"), plain(0x54, "\u041a \u041d\u041e\u041f"),
                                       plain(0x55, "\u041a 1"), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 1).x), "decimal:0:normalized", "const-zero s1.x");
    eq(vt(at(states, 1).x2), "decimal:0:normalized", "const-zero s1.x2");
    eq(vt(at(states, 3).x), "decimal:0:normalized", "const-zero s3.x");
    eq(vt(at(states, 3).x2), "decimal:0:normalized", "const-zero s3.x2");
  }

  // x2 value dataflow follows numeric direct call targets and returns
  {
    const std::vector<IrOp> program = {numeric_call(4), plain(0x54, "\u041a \u041d\u041e\u041f"),
                                       halt(), recall("6"), ret()};
    const auto states = compute(program);
    eq(vt(at(states, 1).x), "reg:6", "numeric-call s1.x");
    eq(vt(at(states, 1).x2), "reg:6", "numeric-call s1.x2");
  }

  // x2 value dataflow follows numeric direct conditional targets
  {
    const std::vector<IrOp> program = {recall("6"), numeric_cjump(4), halt(),
                                       plain(0x54, "\u041a \u041d\u041e\u041f")};
    const auto states = compute(program);
    eq(vt(at(states, 3).x), "reg:6", "numeric-cjump s3.x");
    eq(vt(at(states, 3).x2), "reg:6", "numeric-cjump s3.x2");
  }

  // x2 value dataflow treats resumable stops as reset fallthroughs
  {
    const std::vector<IrOp> program = {recall("6"), pause(), plain(0x54, "\u041a \u041d\u041e\u041f")};
    const auto states = compute(program);
    eq(vt(at(states, 2).x), "", "resumable-stop s2.x");
    eq(vt(at(states, 2).x2), "", "resumable-stop s2.x2");
  }

  // x2 value dataflow tracks normalized decimal digit runs
  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x02, "2"), store("2"), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 2).x), "decimal:12:normalized", "digit-run s2.x");
    eq(vt(at(states, 2).x2), "decimal:12:normalized", "digit-run s2.x2");
    eq(vt(at(states, 3).x), "decimal:12:normalized,reg:2", "digit-run s3.x");
    eq(vt(at(states, 3).x2), "decimal:12:normalized,reg:2", "digit-run s3.x2");
  }

  // x2 value dataflow reads decimal preload facts from recall metadata
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8.1020088E14"), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 1).x), "decimal:810200880000000:normalized,reg:2", "preload-decimal s1.x");
    eq(vt(at(states, 1).x2), "decimal:810200880000000:normalized,reg:2", "preload-decimal s1.x2");
    eq(vt(at(states, 1).xShape), "exponent:8.1020088:14:decimal", "preload-decimal s1.xShape");
    eq(vt(at(states, 1).x2Shape), "exponent:8.1020088:14:decimal", "preload-decimal s1.x2Shape");
  }

  // x2 value dataflow keeps hex-like preload constants as shape-only facts
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8.70\u04152-6\u0421"), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 1).x), "reg:2", "preload-hex s1.x");
    eq(vt(at(states, 1).x2), "reg:2", "preload-hex s1.x2");
    eq(vt(at(states, 1).xShape), "hex:8.70\u04152-6\u0421:mantissa", "preload-hex s1.xShape");
    eq(vt(at(states, 1).x2Shape), "hex:8.70\u04152-6\u0421:mantissa", "preload-hex s1.x2Shape");
  }

  // x2 value dataflow rejects unknown Cyrillic glyphs as structural hex facts
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8\u0416000000"), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 1).x), "reg:2", "reject-glyph s1.x");
    eq(vt(at(states, 1).x2), "reg:2", "reject-glyph s1.x2");
    eq(vt(at(states, 1).xShape), "", "reject-glyph s1.xShape");
    eq(vt(at(states, 1).x2Shape), "", "reject-glyph s1.x2Shape");
  }

  // x2 value dataflow rejects malformed structural mantissa shapes
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8..A"), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 1).x), "reg:2", "reject-malformed s1.x");
    eq(vt(at(states, 1).x2), "reg:2", "reject-malformed s1.x2");
    eq(vt(at(states, 1).xShape), "", "reject-malformed s1.xShape");
    eq(vt(at(states, 1).x2Shape), "", "reject-malformed s1.x2Shape");
  }

  // x2 value dataflow gives closed super sign-change a structural expr key
  {
    const std::vector<IrOp> program = {recall("2", "preload const FA"), plain(0x0b, "/-/"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 2).x), "expr-key:0B(shape:super:FA),expr:1", "super-sign s2.x");
    eq(vt(at(states, 2).x2), "expr-key:0B(shape:super:FA),expr:1", "super-sign s2.x2");
    eq(vt(at(states, 2).xShape), "super:-FA", "super-sign s2.xShape");
    eq(vt(at(states, 2).x2Shape), "super:-FA", "super-sign s2.x2Shape");
  }

  // x2 value dataflow gives closed hex sign-change a structural expr key
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8.70\u04152-6\u0421"),
                                       plain(0x0b, "/-/"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 2).x), "expr-key:0B(shape:hex:8.70\u04152-6\u0421:mantissa),expr:1",
       "hex-sign s2.x");
    eq(vt(at(states, 2).x2), "expr-key:0B(shape:hex:8.70\u04152-6\u0421:mantissa),expr:1",
       "hex-sign s2.x2");
    eq(vt(at(states, 2).xShape), "hex:-8.70\u04152-6\u0421:mantissa", "hex-sign s2.xShape");
    eq(vt(at(states, 2).x2Shape), "hex:-8.70\u04152-6\u0421:mantissa", "hex-sign s2.x2Shape");
  }

  // x2 value dataflow recalls stored super shape facts
  {
    const std::vector<IrOp> program = {recall("2", "preload const FA"), store("1"),
                                       plain(0x0d, "Cx"), recall("1"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 4).x), "reg:1", "recall-super s4.x");
    eq(vt(at(states, 4).x2), "reg:1", "recall-super s4.x2");
    eq(vt(at(states, 4).xShape), "super:FA", "recall-super s4.xShape");
    eq(vt(at(states, 4).x2Shape), "super:FA", "recall-super s4.x2Shape");
  }

  // x2 value dataflow canonicalizes stored structural shape facts
  {
    const std::vector<IrOp> program = {recall("2", "preload const fa.ce"), store("1"),
                                       plain(0x0d, "Cx"), recall("1"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 1).xShape), "hex:FA.CE:mantissa", "canon s1.xShape");
    eq(vt(at(states, 4).xShape), "hex:FA.CE:mantissa", "canon s4.xShape");
    eq(vt(at(states, 4).x2Shape), "hex:FA.CE:mantissa", "canon s4.x2Shape");
  }

  // x2 value dataflow tracks structural Y shapes through stack lift and X/Y exchange
  {
    const std::vector<IrOp> program = {recall("2", "preload const FACE"), plain(0x0e, "\u0412\u2191"),
                                       recall("3", "preload const CAFE"), plain(0x14, "X\u2194Y"),
                                       halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 2).yShape), "hex:FACE:mantissa", "ystruct s2.yShape");
    eq(vt(at(states, 3).xShape), "hex:CAFE:mantissa", "ystruct s3.xShape");
    eq(vt(at(states, 3).yShape), "hex:FACE:mantissa", "ystruct s3.yShape");
    eq(vt(at(states, 4).xShape), "hex:FACE:mantissa", "ystruct s4.xShape");
    eq(vt(at(states, 4).yShape), "hex:CAFE:mantissa", "ystruct s4.yShape");
  }

  // x2 value dataflow stores structural shapes restored from Y by X/Y exchange
  {
    const std::vector<IrOp> program = {recall("2", "preload const FACE"), plain(0x0e, "\u0412\u2191"),
                                       recall("3", "preload const CAFE"), plain(0x14, "X\u2194Y"),
                                       store("1"), plain(0x0d, "Cx"), recall("1"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 5).xShape), "hex:FACE:mantissa", "ystore s5.xShape");
    eq(vt(at(states, 7).xShape), "hex:FACE:mantissa", "ystore s7.xShape");
    eq(vt(at(states, 7).x2Shape), "hex:FACE:mantissa", "ystore s7.x2Shape");
  }

  // x2 value dataflow recalls stored hex sign-change structural expr keys
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8.70\u04152-6\u0421"), store("1"),
                                       plain(0x0d, "Cx"), recall("1"), plain(0x0b, "/-/"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 5).x), "expr-key:0B(shape:hex:8.70\u04152-6\u0421:mantissa),expr:4",
       "recall-hex-sign s5.x");
    eq(vt(at(states, 5).x2), "expr-key:0B(shape:hex:8.70\u04152-6\u0421:mantissa),expr:4",
       "recall-hex-sign s5.x2");
    eq(vt(at(states, 5).xShape), "hex:-8.70\u04152-6\u0421:mantissa", "recall-hex-sign s5.xShape");
    eq(vt(at(states, 5).x2Shape), "hex:-8.70\u04152-6\u0421:mantissa", "recall-hex-sign s5.x2Shape");
  }

  // x2 value dataflow keeps structural abs shape-only
  {
    const std::vector<IrOp> program = {recall("2", "preload const -FACE"), plain(0x31, "\u041a |x|"),
                                       halt()};
    const std::vector<IrOp> exponent_program = {
        recall("2", "preload const -FACE"), plain(0x0c, "\u0412\u041f"), plain(0x01, "1"),
        plain(0x31, "\u041a |x|"), halt()};
    const auto states = compute(program, true);
    const auto exponent_states = compute(exponent_program, true);
    truthy(has(at(states, 2).x, "expr-key:31(shape:hex:-FACE:mantissa)"), "abs s2.x contains");
    eq(vt(at(states, 2).xShape), "hex:FACE:mantissa", "abs s2.xShape");
    eq(vt(at(states, 2).x2Shape), "hex:-FACE:mantissa", "abs s2.x2Shape");
    eq(vt(at(exponent_states, 4).xShape), "hex:FACE0:mantissa", "abs exp s4.xShape");
    eq(vt(at(exponent_states, 4).x2Shape), "hex-exponent:-FACE:1", "abs exp s4.x2Shape");
  }

  // x2 value dataflow tracks structural VP-entry shape context
  {
    const std::vector<IrOp> program = {recall("2", "preload const 8.70\u04152-6\u0421"),
                                       plain(0x0b, "/-/"), plain(0x54, "\u041a \u041d\u041e\u041f"),
                                       halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 1).vpEntryShape), "hex:8.70\u04152-6\u0421:mantissa", "vpentry s1");
    eq(vt(at(states, 2).vpEntryShape), "hex:-8.70\u04152-6\u0421:mantissa", "vpentry s2");
    eq(vt(at(states, 3).vpEntryShape), "hex:-8.70\u04152-6\u0421:mantissa", "vpentry s3");
  }

  // x2 value dataflow treats closed structural exponent shifts as structural display equality
  // (the x2ShapeSetsHaveSameStructuralShape line is an internal predicate, not on the
  // native public surface; the two observable x2Shape assertions are ported.)
  {
    const std::vector<IrOp> exponent_program = {recall("1", "preload const \u0413"),
                                                plain(0x0c, "\u0412\u041f"), plain(0x02, "2"),
                                                plain(0x20, "F\u03c0"), halt()};
    const std::vector<IrOp> mantissa_program = {recall("1", "preload const \u041300"), halt()};
    const auto exponent_states = compute(exponent_program, true);
    const auto mantissa_states = compute(mantissa_program, true);
    eq(vt(at(exponent_states, 4).x2Shape), "hex-exponent:\u0413:2", "exp-equality exp s4.x2Shape");
    eq(vt(at(mantissa_states, 1).x2Shape), "hex:\u041300:mantissa", "exp-equality man s1.x2Shape");
  }

  // x2 value dataflow exposes closed structural exponent sync as a VP mantissa source
  {
    const std::vector<IrOp> program = {
        recall("1", "preload const \u0413"), plain(0x0c, "\u0412\u041f"),  plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),          plain(0x0c, "\u0412\u041f"), plain(0x03, "3"),
        halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 4).xShape), "hex-exponent:\u0413:2", "exp-sync s4.xShape");
    eq(vt(at(states, 4).x2Shape), "hex-exponent:\u0413:2", "exp-sync s4.x2Shape");
    eq(vt(at(states, 4).vpEntryShape), "hex:\u041300:mantissa", "exp-sync s4.vpEntryShape");
    eq(vt(at(states, 5).xShape), "hex-exponent:\u041300:", "exp-sync s5.xShape");
    eq(vt(at(states, 6).xShape), "hex-exponent:\u041300:3", "exp-sync s6.xShape");
  }

  // x2 value dataflow exposes exact decimal exponent display sync as a VP source
  // (the analyzeX2VpShapeContext assertions are an internal predicate, not on the
  // native public surface; the observable state assertions are ported.)
  {
    const std::vector<IrOp> program = {plain(0x01, "1"),  plain(0x0c, "\u0412\u041f"),
                                       plain(0x08, "8"),  plain(0xf0, "F* empty F0"),
                                       plain(0x0c, "\u0412\u041f"), plain(0x03, "3"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 4).xShape), "exponent:1:8:decimal", "dec-exp s4.xShape");
    eq(vt(at(states, 4).x2Shape), "exponent:1:8:decimal", "dec-exp s4.x2Shape");
    truthy(has(at(states, 4).x2, "decimal:100000000:normalized"), "dec-exp s4.x2 contains");
    eq(vt(at(states, 4).vpEntryShape), "", "dec-exp s4.vpEntryShape");
    eq(vt(at(states, 4).vpEntryMantissa), "100000000", "dec-exp s4.vpEntryMantissa");
    eq(vt(at(states, 6).x2Shape), "exponent:100000000:3:decimal,exponent:1:11:decimal",
       "dec-exp s6.x2Shape");
    eq(vt(at(states, 6).x2), "", "dec-exp s6.x2");
  }

  // x2 value dataflow restores synced exact decimal display shapes through dot
  {
    const std::vector<IrOp> program = {plain(0x01, "1"), plain(0x0c, "\u0412\u041f"),
                                       plain(0x08, "8"), plain(0xf0, "F* empty F0"),
                                       plain(0x0a, "."), halt()};
    const auto states = compute(program);
    eq(vt(at(states, 3).x2), "", "dot s3.x2");
    eq(vt(at(states, 3).x2Shape), "exponent:1:8:decimal", "dot s3.x2Shape");
    truthy(has(at(states, 4).x2, "decimal:100000000:normalized"), "dot s4.x2 contains");
    truthy(has(at(states, 5).x, "decimal:100000000:normalized"), "dot s5.x contains");
  }

  // x2 value dataflow derives VP mantissa sources from conditional structural X2 syncs
  {
    const std::vector<IrOp> program = {
        recall("1", "preload const \u0413"), plain(0x0c, "\u0412\u041f"),  plain(0x02, "2"),
        cjump("done"),                       plain(0x0c, "\u0412\u041f"), plain(0x03, "3"),
        halt(),                              label("done"),                halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 4).xShape), "hex-exponent:\u0413:2", "cond-vp s4.xShape");
    eq(vt(at(states, 4).x2Shape), "hex-exponent:\u0413:2", "cond-vp s4.x2Shape");
    eq(vt(at(states, 4).vpEntryShape), "hex:\u041300:mantissa", "cond-vp s4.vpEntryShape");
    eq(vt(at(states, 5).xShape), "hex-exponent:\u041300:", "cond-vp s5.xShape");
    eq(vt(at(states, 6).xShape), "hex-exponent:\u041300:3", "cond-vp s6.xShape");
  }

  // x2 value dataflow joins structural exponent and mantissa shapes by restored display shape
  {
    const std::vector<IrOp> program = {
        recall("1", "preload const \u0413"),  cjump("right"),
        plain(0x0c, "\u0412\u041f"),           plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),            jump("join"),
        label("right"),                        recall("2", "preload const \u041300"),
        label("join"),                         plain(0x0c, "\u0412\u041f"),
        plain(0x03, "3"),                      halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 9).xShape), "hex:\u041300:mantissa", "join-exp s9.xShape");
    eq(vt(at(states, 9).x2Shape), "hex:\u041300:mantissa", "join-exp s9.x2Shape");
    eq(vt(at(states, 9).vpEntryShape), "hex:\u041300:mantissa", "join-exp s9.vpEntryShape");
    eq(vt(at(states, 10).xShape), "hex-exponent:\u041300:", "join-exp s10.xShape");
    eq(vt(at(states, 11).xShape), "hex-exponent:\u041300:3", "join-exp s11.xShape");
  }

  // x2 value dataflow joins structural shape memory by restored display shape
  {
    const std::vector<IrOp> program = {
        recall("1", "preload const \u0413"),  cjump("right"),
        plain(0x0c, "\u0412\u041f"),           plain(0x02, "2"),
        plain(0xf0, "F* empty F0"),            store("3"),
        jump("join"),                          label("right"),
        recall("2", "preload const \u041300"), store("3"),
        label("join"),                         plain(0x0d, "Cx"),
        recall("3"),                           halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 13).xShape), "hex:\u041300:mantissa", "join-mem s13.xShape");
    eq(vt(at(states, 13).x2Shape), "hex:\u041300:mantissa", "join-mem s13.x2Shape");
  }

  // x2 value dataflow parses preloaded structural exponent notation
  {
    const std::vector<IrOp> program = {recall("1", "preload const \u0413E-2"),
                                       recall("2", "preload const FAE2"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 1).xShape), "hex-exponent:\u0413:-2", "parse-exp s1.xShape");
    eq(vt(at(states, 2).xShape), "super-exponent:FA:2", "parse-exp s2.xShape");
  }

  // x2 value dataflow gives preloaded decimal constants display-accurate shapes
  {
    const std::vector<IrOp> program = {recall("1", "preload const 12.3"),
                                       recall("2", "preload const 1E8"),
                                       recall("3", "preload const 1E-8"), halt()};
    const auto states = compute(program, true);
    eq(vt(at(states, 1).x), "decimal:12.3:normalized,reg:1", "dec-const s1.x");
    eq(vt(at(states, 1).xShape), "mantissa:12.3:decimal", "dec-const s1.xShape");
    eq(vt(at(states, 2).x), "decimal:100000000:normalized,reg:2", "dec-const s2.x");
    eq(vt(at(states, 2).xShape), "exponent:1:8:decimal", "dec-const s2.xShape");
    eq(vt(at(states, 3).x), "decimal:0.00000001:normalized,reg:3", "dec-const s3.x");
    eq(vt(at(states, 3).xShape), "exponent:1:-8:decimal", "dec-const s3.xShape");
  }

  // ---- transfer_x2_value_state_for_edge cases (hand-built before-states) ----
  using P::X2DataflowEdgeKind;
  const auto edge_transfer = [](const std::optional<X2ValueDataflowState>& input, const IrOp& op,
                                X2DataflowEdgeKind edge, bool track_memory,
                                int producer_index) -> std::optional<X2ValueDataflowState> {
    if (!input.has_value()) return std::nullopt;
    P::X2TransferStateOptions options;
    options.track_register_memory = track_memory;
    return P::transfer_x2_value_state_for_edge(input, op, edge, options, producer_index);
  };
  const auto req = [](const std::optional<X2ValueDataflowState>& s,
                      const std::string& label) -> const X2ValueDataflowState& {
    require(s.has_value(), "x2 value dataflow [" + label + "] expected defined state");
    return *s;
  };

  // x2 value dataflow preserves direct Y shape through structural ВП sources
  {
    X2ValueDataflowState before;
    before.y = Set{};
    before.yShape = Set{"hex:A0:mantissa"};
    before.xDirectShape = Set{};
    before.yDirectShape = Set{"hex:A0:mantissa"};
    before.vpEntryShape = Set{"hex:B:mantissa"};
    const auto after_vp =
        edge_transfer(before, plain(0x0c, "\u0412\u041f"), X2DataflowEdgeKind::Normal, false, 0);
    const auto after_clear =
        edge_transfer(after_vp, plain(0x0d, "Cx"), X2DataflowEdgeKind::Normal, false, 1);
    const auto after_digit =
        edge_transfer(after_clear, plain(0x01, "1"), X2DataflowEdgeKind::Normal, false, 2);
    const auto after_plus =
        edge_transfer(after_digit, plain(0x10, "+"), X2DataflowEdgeKind::Normal, false, 3);
    eq(vt(req(after_vp, "vp-src after_vp").yDirectShape), "hex:A0:mantissa", "vp-src yDirectShape");
    truthy(has(req(after_plus, "vp-src after_plus").x, "decimal:101:normalized"), "vp-src x");
    truthy(has(req(after_plus, "vp-src after_plus").xShape, "mantissa:101:decimal"),
           "vp-src xShape");
  }

  // x2 value dataflow preserves direct Y shape through structural ВП exponent context
  {
    X2ValueDataflowState before;
    before.y = Set{};
    before.yShape = Set{"hex:A0:mantissa"};
    before.xDirectShape = Set{};
    before.yDirectShape = Set{"hex:A0:mantissa"};
    before.structuralVpContext.kind = P::X2StructuralEntryState::Kind::Exponent;
    before.structuralVpContext.mantissa = Set{"hex:B:mantissa"};
    before.structuralVpContext.exponent = Set{"2"};
    before.vpEntryShape = Set{"hex:B:mantissa"};
    const auto after_vp =
        edge_transfer(before, plain(0x0c, "\u0412\u041f"), X2DataflowEdgeKind::Normal, false, 0);
    eq(vt(req(after_vp, "vp-exp after_vp").yDirectShape), "hex:A0:mantissa", "vp-exp yDirectShape");
    eq(vt(req(after_vp, "vp-exp after_vp").x2Shape), "hex-exponent:B:2", "vp-exp x2Shape");
  }

  // x2 value dataflow preserves direct Y shape through unknown ВП context
  {
    X2ValueDataflowState before;
    before.y = Set{};
    before.yShape = Set{"hex:A0:mantissa"};
    before.xDirectShape = Set{};
    before.yDirectShape = Set{"hex:A0:mantissa"};
    const auto after_vp =
        edge_transfer(before, plain(0x0c, "\u0412\u041f"), X2DataflowEdgeKind::Normal, false, 0);
    const auto after_clear =
        edge_transfer(after_vp, plain(0x0d, "Cx"), X2DataflowEdgeKind::Normal, false, 1);
    const auto after_digit =
        edge_transfer(after_clear, plain(0x01, "1"), X2DataflowEdgeKind::Normal, false, 2);
    const auto after_plus =
        edge_transfer(after_digit, plain(0x10, "+"), X2DataflowEdgeKind::Normal, false, 3);
    eq(entry_text(req(after_vp, "vp-unk after_vp").entry), "unknown", "vp-unk entry");
    eq(vt(req(after_vp, "vp-unk after_vp").yDirectShape), "hex:A0:mantissa", "vp-unk yDirectShape");
    truthy(has(req(after_plus, "vp-unk after_plus").x, "decimal:101:normalized"), "vp-unk x");
    truthy(has(req(after_plus, "vp-unk after_plus").xShape, "mantissa:101:decimal"),
           "vp-unk xShape");
  }

  // x2 value dataflow preserves direct Y shape through structural closed sign sources
  {
    X2ValueDataflowState before;
    before.y = Set{};
    before.yShape = Set{"hex:A0:mantissa"};
    before.xDirectShape = Set{};
    before.yDirectShape = Set{"hex:A0:mantissa"};
    before.vpEntrySignShape = Set{"hex:B:mantissa"};
    const auto after_sign =
        edge_transfer(before, plain(0x0b, "/-/"), X2DataflowEdgeKind::Normal, false, 0);
    const auto after_clear =
        edge_transfer(after_sign, plain(0x0d, "Cx"), X2DataflowEdgeKind::Normal, false, 1);
    const auto after_digit =
        edge_transfer(after_clear, plain(0x01, "1"), X2DataflowEdgeKind::Normal, false, 2);
    const auto after_plus =
        edge_transfer(after_digit, plain(0x10, "+"), X2DataflowEdgeKind::Normal, false, 3);
    eq(vt(req(after_sign, "sign after_sign").x2Shape), "hex:-B:mantissa", "sign x2Shape");
    eq(vt(req(after_sign, "sign after_sign").yDirectShape), "hex:A0:mantissa", "sign yDirectShape");
    truthy(has(req(after_plus, "sign after_plus").x, "decimal:101:normalized"), "sign x");
    truthy(has(req(after_plus, "sign after_plus").xShape, "mantissa:101:decimal"), "sign xShape");
  }

  // x2 value dataflow preserves direct shapes through indirect flow and memory effects
  {
    struct EdgeCase {
      IrOp op;
      X2DataflowEdgeKind edge;
      bool track_memory;
      std::string label;
    };
    const std::vector<EdgeCase> cases = {
        {known_target_indirect_cjump("7", 5), X2DataflowEdgeKind::Fallthrough, false, "ind-cjump"},
        {known_target_indirect_store("7", "1"), X2DataflowEdgeKind::Normal, true, "ind-store-known"},
        {indirect_store("7"), X2DataflowEdgeKind::Normal, true, "ind-store"}};
    for (const auto& c : cases) {
      X2ValueDataflowState before;
      before.xShape = Set{"hex:C0:mantissa"};
      before.yShape = Set{"hex:A0:mantissa"};
      before.xDirectShape = Set{"hex:C0:mantissa"};
      before.yDirectShape = Set{"hex:A0:mantissa"};
      const auto after_op = edge_transfer(before, c.op, c.edge, c.track_memory, 0);
      const auto after_clear =
          edge_transfer(after_op, plain(0x0d, "Cx"), X2DataflowEdgeKind::Normal, false, 1);
      const auto after_digit =
          edge_transfer(after_clear, plain(0x01, "1"), X2DataflowEdgeKind::Normal, false, 2);
      const auto after_plus =
          edge_transfer(after_digit, plain(0x10, "+"), X2DataflowEdgeKind::Normal, false, 3);
      eq(vt(req(after_op, c.label + " after_op").xDirectShape), "hex:C0:mantissa",
         c.label + " xDirectShape");
      eq(vt(req(after_op, c.label + " after_op").yDirectShape), "hex:A0:mantissa",
         c.label + " yDirectShape");
      truthy(has(req(after_plus, c.label + " after_plus").x, "decimal:101:normalized"),
             c.label + " x");
      truthy(has(req(after_plus, c.label + " after_plus").xShape, "mantissa:101:decimal"),
             c.label + " xShape");
    }
  }
}

}  // namespace mkpro::tests
