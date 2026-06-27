#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::X2ShapeSet;
using mkpro::core::passes::X2ValueSet;
using mkpro::core::passes::x2_stable_expression_debug;

void expect(int opcode, bool has_producer, int producer, const std::vector<std::string>& x,
            const std::vector<std::string>& y, const std::vector<std::string>& xs,
            const std::vector<std::string>& ys, const std::vector<std::string>& dxs,
            const std::vector<std::string>& dys, const std::string& expected) {
  const X2ValueSet x_set(x.begin(), x.end());
  const X2ValueSet y_set(y.begin(), y.end());
  const X2ShapeSet xs_set(xs.begin(), xs.end());
  const X2ShapeSet ys_set(ys.begin(), ys.end());
  const X2ShapeSet dxs_set(dxs.begin(), dxs.end());
  const X2ShapeSet dys_set(dys.begin(), dys.end());
  const std::string actual = x2_stable_expression_debug(opcode, has_producer, producer, x_set, y_set,
                                                        xs_set, ys_set, dxs_set, dys_set);
  require(actual == expected, "stable expression op=0x" + std::to_string(opcode) + " expected\n  " +
                                  expected + "\ngot\n  " + actual);
}

}  // namespace

void x2_stable_expression_matches_typescript_contract() {
  expect(0x21, true, 2, {"reg:0"}, {}, {}, {}, {}, {}, "v=expr-key:21(reg:0),expr:2|s=_");
  expect(0x16, true, 5, {"reg:0"}, {}, {}, {}, {}, {}, "v=expr-key:16(reg:0),expr:5|s=_");
  expect(0x21, false, 0, {"reg:1"}, {}, {}, {}, {}, {}, "v=expr-key:21(reg:1)|s=_");
  expect(0x21, true, 3, {"decimal:4:normalized"}, {}, {}, {}, {}, {}, "v=decimal:2:normalized,expr:3|s=mantissa:2:decimal");
  expect(0x22, false, 0, {"decimal:3:normalized"}, {}, {"mantissa:3:decimal"}, {}, {}, {}, "v=decimal:9:normalized|s=mantissa:9:decimal");
  expect(0x22, false, 0, {}, {}, {"hex:5:mantissa"}, {}, {}, {}, "v=decimal:25:normalized|s=mantissa:25:decimal");
  expect(0x32, true, 1, {"decimal:-7:normalized"}, {}, {}, {}, {}, {}, "v=decimal:-1:normalized,expr:1|s=mantissa:-1:decimal");
  expect(0x34, false, 0, {"decimal:3.7:normalized"}, {}, {}, {}, {}, {}, "v=decimal:3:normalized|s=mantissa:3:decimal");
  expect(0x35, false, 0, {"decimal:3.25:normalized"}, {}, {}, {}, {}, {}, "v=decimal:0.25:normalized|s=exponent:2.5:-1:decimal");
  expect(0x23, false, 0, {"decimal:4:normalized"}, {}, {}, {}, {}, {}, "v=decimal:0.25:normalized|s=exponent:2.5:-1:decimal");
  expect(0x20, true, 4, {}, {}, {}, {}, {}, {}, "v=decimal:3.1415926:normalized,expr-key:20()|s=mantissa:3.1415926:decimal");
  expect(0x10, true, 6, {"decimal:3:normalized"}, {"decimal:2:normalized"}, {}, {}, {}, {}, "v=decimal:5:normalized,expr:6|s=mantissa:5:decimal");
  expect(0x12, true, 6, {"decimal:3:normalized"}, {"decimal:2:normalized"}, {}, {}, {}, {}, "v=decimal:6:normalized,expr:6|s=mantissa:6:decimal");
  expect(0x11, true, 6, {"decimal:3:normalized"}, {"decimal:10:normalized"}, {}, {}, {}, {}, "v=decimal:7:normalized,expr:6|s=mantissa:7:decimal");
  expect(0x13, true, 6, {"decimal:3:normalized"}, {"decimal:12:normalized"}, {}, {}, {}, {}, "v=decimal:4:normalized,expr:6|s=mantissa:4:decimal");
  expect(0x24, true, 6, {"decimal:3:normalized"}, {"decimal:2:normalized"}, {}, {}, {}, {}, "v=expr-key:24(decimal:2:normalized,decimal:3:normalized),expr:6|s=_");
  expect(0x10, true, 7, {"reg:0"}, {"reg:1"}, {}, {}, {}, {}, "v=expr-key:10(reg:0,reg:1),expr:7|s=_");
  expect(0x12, true, 7, {"reg:1"}, {"reg:0"}, {}, {}, {}, {}, "v=expr-key:12(reg:0,reg:1),expr:7|s=_");
  expect(0x13, true, 7, {"reg:0"}, {"reg:1"}, {}, {}, {}, {}, "v=expr-key:13(reg:1,reg:0),expr:7|s=_");
  expect(0x11, true, 7, {"reg:1"}, {"reg:0"}, {}, {}, {}, {}, "v=expr-key:11(reg:0,reg:1),expr:7|s=_");
  expect(0x10, true, 8, {"reg:0"}, {"decimal:2:normalized"}, {}, {}, {}, {}, "v=expr-key:10(decimal:2:normalized,reg:0),expr:8|s=_");
  expect(0x37, true, 9, {}, {}, {"hex:F:mantissa"}, {"hex:5:mantissa"}, {}, {}, "v=decimal:8:normalized,expr-key:37(decimal:5:normalized,shape:hex:F:mantissa),expr:9|s=hex:8.0000000:mantissa,mantissa:8.0000000:decimal");
  expect(0x38, true, 9, {}, {}, {"hex:C:mantissa"}, {"hex:3:mantissa"}, {}, {}, "v=decimal:8:normalized,expr-key:38(decimal:3:normalized,shape:hex:C:mantissa),expr:9|s=hex:8.0000000:mantissa,mantissa:8.0000000:decimal");
  expect(0x39, true, 9, {}, {}, {"hex:F:mantissa"}, {"hex:A:mantissa"}, {}, {}, "v=decimal:8:normalized,expr-key:39(shape:hex:A:mantissa,shape:hex:F:mantissa),expr:9|s=hex:8.0000000:mantissa,mantissa:8.0000000:decimal");
  expect(0x3a, true, 9, {}, {}, {"hex:5:mantissa"}, {}, {}, {}, "v=expr-key:3A(decimal:5:normalized),expr:9|s=hex:8.FFFFFFF:mantissa");
  expect(0x10, false, 0, {"decimal:5:normalized"}, {}, {"hex:A:mantissa"}, {}, {}, {}, "v=_|s=_");
  expect(0x12, false, 0, {"decimal:5:normalized"}, {}, {"hex:A:mantissa"}, {}, {}, {}, "v=_|s=_");
  expect(0x22, false, 0, {"expr-key:21(decimal:4:normalized)"}, {}, {}, {}, {}, {}, "v=decimal:4:normalized|s=mantissa:4:decimal");
  expect(0x10, true, 10, {"expr-key:21(reg:0)"}, {"reg:1"}, {}, {}, {}, {}, "v=expr-key:10(expr-key:21(reg:0),reg:1),expr:10|s=_");
  expect(0x21, false, 0, {}, {}, {"mantissa:9:decimal"}, {}, {}, {}, "v=decimal:3:normalized|s=mantissa:3:decimal");
  expect(0x31, false, 0, {"decimal:-5:normalized"}, {}, {"mantissa:-5:decimal"}, {}, {}, {}, "v=decimal:5:normalized|s=mantissa:5:decimal");
  expect(0x31, false, 0, {}, {}, {"hex:5:mantissa"}, {}, {}, {}, "v=decimal:5:normalized|s=hex:5:mantissa,mantissa:5:decimal");
}

}  // namespace mkpro::tests
