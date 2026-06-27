#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::X2ShapeSet;
using mkpro::core::passes::X2ValueSet;
using mkpro::core::passes::x2_structural_hex_binary_debug;

void expect(int opcode, const std::vector<std::string>& y, const std::vector<std::string>& x,
            const std::vector<std::string>& ys, const std::vector<std::string>& xs,
            const std::vector<std::string>& dys, const std::vector<std::string>& dxs,
            const std::string& expected) {
  const X2ValueSet y_set(y.begin(), y.end());
  const X2ValueSet x_set(x.begin(), x.end());
  const X2ShapeSet ys_set(ys.begin(), ys.end());
  const X2ShapeSet xs_set(xs.begin(), xs.end());
  const X2ShapeSet dys_set(dys.begin(), dys.end());
  const X2ShapeSet dxs_set(dxs.begin(), dxs.end());
  const std::string actual =
      x2_structural_hex_binary_debug(opcode, y_set, x_set, ys_set, xs_set, dys_set, dxs_set);
  require(actual == expected, "structural hex op=0x" + std::to_string(opcode) + " expected\n  " +
                                  expected + "\ngot\n  " + actual);
}

}  // namespace

void x2_structural_hex_binary_matches_typescript_contract() {
  expect(0x10, {}, {"decimal:5:normalized"}, {"hex:A:mantissa"}, {}, {}, {}, "v=15|disp=mantissa:15:decimal");
  expect(0x11, {}, {"decimal:5:normalized"}, {"hex:A:mantissa"}, {}, {}, {}, "v=5|disp=mantissa:5:decimal");
  expect(0x12, {}, {"decimal:5:normalized"}, {"hex:A:mantissa"}, {}, {}, {}, "v=50|disp=mantissa:50:decimal");
  expect(0x13, {}, {"decimal:5:normalized"}, {"hex:A:mantissa"}, {}, {}, {}, "v=2|disp=mantissa:2:decimal");
  expect(0x10, {}, {"decimal:3:normalized"}, {"hex:C:mantissa"}, {}, {}, {}, "v=15|disp=mantissa:15:decimal");
  expect(0x11, {}, {"decimal:3:normalized"}, {"hex:C:mantissa"}, {}, {}, {}, "v=9|disp=mantissa:9:decimal");
  expect(0x12, {}, {"decimal:3:normalized"}, {"hex:C:mantissa"}, {}, {}, {}, "v=4|disp=mantissa:4:decimal");
  expect(0x13, {}, {"decimal:3:normalized"}, {"hex:C:mantissa"}, {}, {}, {}, "v=4|disp=mantissa:4:decimal");
  expect(0x10, {}, {"decimal:9:normalized"}, {"hex:E:mantissa"}, {}, {}, {}, "v=23|disp=mantissa:23:decimal");
  expect(0x11, {}, {"decimal:9:normalized"}, {"hex:E:mantissa"}, {}, {}, {}, "v=5|disp=mantissa:5:decimal");
  expect(0x12, {}, {"decimal:9:normalized"}, {"hex:E:mantissa"}, {}, {}, {}, "v=82|disp=mantissa:82:decimal");
  expect(0x13, {}, {"decimal:9:normalized"}, {"hex:E:mantissa"}, {}, {}, {}, "v=1.5555555|disp=mantissa:1.5555555:decimal");
  expect(0x10, {}, {"decimal:2:normalized"}, {"hex:F:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x11, {}, {"decimal:2:normalized"}, {"hex:F:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x12, {}, {"decimal:2:normalized"}, {"hex:F:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x13, {}, {"decimal:2:normalized"}, {"hex:F:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x10, {"decimal:7:normalized"}, {}, {}, {"hex:A:mantissa"}, {}, {}, "v=1|disp=mantissa:1:decimal");
  expect(0x11, {"decimal:7:normalized"}, {}, {}, {"hex:A:mantissa"}, {}, {}, "v=-3|disp=mantissa:-3:decimal");
  expect(0x12, {"decimal:7:normalized"}, {}, {}, {"hex:A:mantissa"}, {}, {}, "v=70|disp=mantissa:70:decimal");
  expect(0x13, {"decimal:7:normalized"}, {}, {}, {"hex:A:mantissa"}, {}, {}, "v=_|disp=_");
  expect(0x10, {"decimal:0:normalized"}, {}, {}, {"hex:C:mantissa"}, {}, {}, "v=2|disp=mantissa:2:decimal");
  expect(0x11, {"decimal:0:normalized"}, {}, {}, {"hex:C:mantissa"}, {}, {}, "v=-2|disp=mantissa:-2:decimal");
  expect(0x12, {"decimal:0:normalized"}, {}, {}, {"hex:C:mantissa"}, {}, {}, "v=0|disp=mantissa:0:decimal");
  expect(0x13, {"decimal:0:normalized"}, {}, {}, {"hex:C:mantissa"}, {}, {}, "v=0.99099099|disp=exponent:9.9099099:-1:decimal");
  expect(0x10, {"decimal:11:normalized"}, {}, {}, {"hex:D:mantissa"}, {}, {}, "v=24|disp=mantissa:24:decimal");
  expect(0x11, {"decimal:11:normalized"}, {}, {}, {"hex:D:mantissa"}, {}, {}, "v=14|disp=mantissa:14:decimal");
  expect(0x12, {"decimal:11:normalized"}, {}, {}, {"hex:D:mantissa"}, {}, {}, "v=110|disp=mantissa:110:decimal");
  expect(0x13, {"decimal:11:normalized"}, {}, {}, {"hex:D:mantissa"}, {}, {}, "v=9.8|disp=mantissa:9.8:decimal");
  expect(0x10, {}, {}, {"hex:A:mantissa"}, {"hex:C:mantissa"}, {}, {}, "v=6|disp=mantissa:6:decimal");
  expect(0x11, {}, {}, {"hex:A:mantissa"}, {"hex:C:mantissa"}, {}, {}, "v=-2|disp=mantissa:-2:decimal");
  expect(0x12, {}, {}, {"hex:A:mantissa"}, {"hex:C:mantissa"}, {}, {}, "v=0|disp=mantissa:00:decimal");
  expect(0x13, {}, {}, {"hex:A:mantissa"}, {"hex:C:mantissa"}, {}, {}, "v=_|disp=_");
  expect(0x10, {}, {}, {"hex:C:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=10|disp=mantissa:10:decimal");
  expect(0x11, {}, {}, {"hex:C:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=-2|disp=mantissa:-2:decimal");
  expect(0x12, {}, {}, {"hex:C:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=0|disp=mantissa:0:decimal");
  expect(0x13, {}, {}, {"hex:C:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=0.52929292|disp=exponent:5.2929292:-1:decimal");
  expect(0x10, {}, {}, {"hex:D:mantissa"}, {"hex:D:mantissa"}, {}, {}, "v=10|disp=mantissa:10:decimal");
  expect(0x11, {}, {}, {"hex:D:mantissa"}, {"hex:D:mantissa"}, {}, {}, "v=0|disp=mantissa:0:decimal");
  expect(0x12, {}, {}, {"hex:D:mantissa"}, {"hex:D:mantissa"}, {}, {}, "v=30|disp=mantissa:30:decimal");
  expect(0x13, {}, {}, {"hex:D:mantissa"}, {"hex:D:mantissa"}, {}, {}, "v=1|disp=mantissa:1:decimal");
  expect(0x10, {}, {}, {"hex:E:mantissa"}, {"hex:A:mantissa"}, {}, {}, "v=8|disp=mantissa:8:decimal");
  expect(0x11, {}, {}, {"hex:E:mantissa"}, {"hex:A:mantissa"}, {}, {}, "v=4|disp=mantissa:4:decimal");
  expect(0x12, {}, {}, {"hex:E:mantissa"}, {"hex:A:mantissa"}, {}, {}, "v=40|disp=mantissa:40:decimal");
  expect(0x13, {}, {}, {"hex:E:mantissa"}, {"hex:A:mantissa"}, {}, {}, "v=1.4|disp=mantissa:1.4:decimal");
  expect(0x10, {}, {"decimal:5:normalized"}, {"hex:A0:mantissa"}, {}, {}, {}, "v=5|disp=mantissa:05:decimal");
  expect(0x11, {}, {"decimal:5:normalized"}, {"hex:A0:mantissa"}, {}, {}, {}, "v=-65|disp=mantissa:-65:decimal");
  expect(0x12, {}, {"decimal:5:normalized"}, {"hex:A0:mantissa"}, {}, {}, {}, "v=500|disp=mantissa:500:decimal");
  expect(0x13, {}, {"decimal:5:normalized"}, {"hex:A0:mantissa"}, {}, {}, {}, "v=20|disp=mantissa:20:decimal");
  expect(0x10, {}, {"decimal:2:normalized"}, {"hex:C00:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x11, {}, {"decimal:2:normalized"}, {"hex:C00:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x12, {}, {"decimal:2:normalized"}, {"hex:C00:mantissa"}, {}, {}, {}, "v=800|disp=mantissa:800:decimal");
  expect(0x13, {}, {"decimal:2:normalized"}, {"hex:C00:mantissa"}, {}, {}, {}, "v=600|disp=mantissa:600:decimal");
  expect(0x10, {"decimal:4:normalized"}, {}, {}, {"hex:A0:mantissa"}, {}, {}, "v=4|disp=mantissa:04:decimal");
  expect(0x11, {"decimal:4:normalized"}, {}, {}, {"hex:A0:mantissa"}, {}, {}, "v=-96|disp=mantissa:-96:decimal");
  expect(0x12, {"decimal:4:normalized"}, {}, {}, {"hex:A0:mantissa"}, {}, {}, "v=400|disp=mantissa:400:decimal");
  expect(0x13, {"decimal:4:normalized"}, {}, {}, {"hex:A0:mantissa"}, {}, {}, "v=_|disp=_");
  expect(0x10, {}, {"decimal:3:normalized"}, {"hex:0.0E:mantissa"}, {}, {}, {}, "v=3.14|disp=mantissa:3.14:decimal");
  expect(0x11, {}, {"decimal:3:normalized"}, {"hex:0.0E:mantissa"}, {}, {}, {}, "v=-2.86|disp=mantissa:-2.86:decimal");
  expect(0x12, {}, {"decimal:3:normalized"}, {"hex:0.0E:mantissa"}, {}, {}, {}, "v=0.1|disp=exponent:1:-1:decimal");
  expect(0x13, {}, {"decimal:3:normalized"}, {"hex:0.0E:mantissa"}, {}, {}, {}, "v=0.046666666|disp=exponent:4.6666666:-2:decimal");
  expect(0x10, {"decimal:6:normalized"}, {}, {}, {"hex:0.0A:mantissa"}, {}, {}, "v=6.1|disp=mantissa:6.1:decimal");
  expect(0x11, {"decimal:6:normalized"}, {}, {}, {"hex:0.0A:mantissa"}, {}, {}, "v=5.9|disp=mantissa:5.9:decimal");
  expect(0x12, {"decimal:6:normalized"}, {}, {}, {"hex:0.0A:mantissa"}, {}, {}, "v=0.6|disp=exponent:6:-1:decimal");
  expect(0x13, {"decimal:6:normalized"}, {}, {}, {"hex:0.0A:mantissa"}, {}, {}, "v=_|disp=_");
  expect(0x10, {}, {"decimal:5:normalized"}, {"exponent:A:1:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x11, {}, {"decimal:5:normalized"}, {"exponent:A:1:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x12, {}, {"decimal:5:normalized"}, {"exponent:A:1:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x13, {}, {"decimal:5:normalized"}, {"exponent:A:1:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x10, {}, {"decimal:8:normalized"}, {"exponent:C:-2:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x11, {}, {"decimal:8:normalized"}, {"exponent:C:-2:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x12, {}, {"decimal:8:normalized"}, {"exponent:C:-2:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x13, {}, {"decimal:8:normalized"}, {"exponent:C:-2:hex"}, {}, {}, {}, "v=_|disp=_");
  expect(0x10, {}, {"decimal:5:normalized"}, {}, {}, {"hex:1A:mantissa"}, {}, "v=25|disp=mantissa:25:decimal");
  expect(0x11, {}, {"decimal:5:normalized"}, {}, {}, {"hex:1A:mantissa"}, {}, "v=15|disp=mantissa:15:decimal");
  expect(0x12, {}, {"decimal:5:normalized"}, {}, {}, {"hex:1A:mantissa"}, {}, "v=100|disp=mantissa:100:decimal");
  expect(0x13, {}, {"decimal:5:normalized"}, {}, {}, {"hex:1A:mantissa"}, {}, "v=4|disp=mantissa:4:decimal");
  expect(0x10, {"decimal:7:normalized"}, {}, {}, {}, {}, {"hex:2C:mantissa"}, "v=39|disp=mantissa:39:decimal");
  expect(0x11, {"decimal:7:normalized"}, {}, {}, {}, {}, {"hex:2C:mantissa"}, "v=-25|disp=mantissa:-25:decimal");
  expect(0x12, {"decimal:7:normalized"}, {}, {}, {}, {}, {"hex:2C:mantissa"}, "v=224|disp=mantissa:224:decimal");
  expect(0x13, {"decimal:7:normalized"}, {}, {}, {}, {}, {"hex:2C:mantissa"}, "v=0.21875|disp=exponent:2.1875:-1:decimal");
  expect(0x10, {}, {"decimal:3:normalized"}, {}, {}, {"hex:AB:mantissa"}, {}, "v=114|disp=mantissa:114:decimal");
  expect(0x11, {}, {"decimal:3:normalized"}, {}, {}, {"hex:AB:mantissa"}, {}, "v=108|disp=mantissa:108:decimal");
  expect(0x12, {}, {"decimal:3:normalized"}, {}, {}, {"hex:AB:mantissa"}, {}, "v=333|disp=mantissa:333:decimal");
  expect(0x13, {}, {"decimal:3:normalized"}, {}, {}, {"hex:AB:mantissa"}, {}, "v=37|disp=mantissa:37:decimal");
  expect(0x10, {}, {}, {"hex:A:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=8|disp=mantissa:8:decimal");
  expect(0x11, {}, {}, {"hex:A:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=-4|disp=mantissa:-4:decimal");
  expect(0x12, {}, {}, {"hex:A:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=0|disp=mantissa:0:decimal");
  expect(0x13, {}, {}, {"hex:A:mantissa"}, {"hex:E:mantissa"}, {}, {}, "v=0.52929292|disp=exponent:5.2929292:-1:decimal");
  expect(0x10, {}, {}, {"mantissa:5:decimal"}, {"hex:C:mantissa"}, {}, {}, "v=1|disp=mantissa:1:decimal");
  expect(0x11, {}, {}, {"mantissa:5:decimal"}, {"hex:C:mantissa"}, {}, {}, "v=-7|disp=mantissa:-7:decimal");
  expect(0x12, {}, {}, {"mantissa:5:decimal"}, {"hex:C:mantissa"}, {}, {}, "v=50|disp=mantissa:50:decimal");
  expect(0x13, {}, {}, {"mantissa:5:decimal"}, {"hex:C:mantissa"}, {}, {}, "v=_|disp=_");
  expect(0x10, {}, {}, {"hex:A:mantissa"}, {"mantissa:3:decimal"}, {}, {}, "v=13|disp=mantissa:13:decimal");
  expect(0x11, {}, {}, {"hex:A:mantissa"}, {"mantissa:3:decimal"}, {}, {}, "v=7|disp=mantissa:7:decimal");
  expect(0x12, {}, {}, {"hex:A:mantissa"}, {"mantissa:3:decimal"}, {}, {}, "v=4|disp=mantissa:4:decimal");
  expect(0x13, {}, {}, {"hex:A:mantissa"}, {"mantissa:3:decimal"}, {}, {}, "v=3.3333333|disp=mantissa:3.3333333:decimal");
  expect(0x10, {}, {"decimal:1:normalized"}, {"hex:E0:mantissa"}, {}, {}, {}, "v=41|disp=mantissa:41:decimal");
  expect(0x11, {}, {"decimal:1:normalized"}, {"hex:E0:mantissa"}, {}, {}, {}, "v=-21|disp=mantissa:-21:decimal");
  expect(0x12, {}, {"decimal:1:normalized"}, {"hex:E0:mantissa"}, {}, {}, {}, "v=40|disp=mantissa:40:decimal");
  expect(0x13, {}, {"decimal:1:normalized"}, {"hex:E0:mantissa"}, {}, {}, {}, "v=40|disp=mantissa:40:decimal");
  expect(0x10, {}, {"decimal:9:normalized"}, {"hex:A00:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x11, {}, {"decimal:9:normalized"}, {"hex:A00:mantissa"}, {}, {}, {}, "v=_|disp=_");
  expect(0x12, {}, {"decimal:9:normalized"}, {"hex:A00:mantissa"}, {}, {}, {}, "v=3000|disp=mantissa:3000:decimal");
  expect(0x13, {}, {"decimal:9:normalized"}, {"hex:A00:mantissa"}, {}, {}, {}, "v=111.11111|disp=mantissa:111.11111:decimal");
}

}  // namespace mkpro::tests
