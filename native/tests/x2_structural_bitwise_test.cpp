#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::X2ShapeSet;
using mkpro::core::passes::X2ValueSet;
using mkpro::core::passes::x2_structural_bitwise_binary_debug;

void expect(int opcode, const std::vector<std::string>& y, const std::vector<std::string>& x,
            const std::vector<std::string>& ys, const std::vector<std::string>& xs,
            const std::string& expected) {
  const X2ValueSet y_set(y.begin(), y.end());
  const X2ValueSet x_set(x.begin(), x.end());
  const X2ShapeSet ys_set(ys.begin(), ys.end());
  const X2ShapeSet xs_set(xs.begin(), xs.end());
  const std::string actual = x2_structural_bitwise_binary_debug(opcode, y_set, x_set, ys_set, xs_set);
  require(actual == expected,
          "structural bitwise op=0x" + std::to_string(opcode) + " expected\n  " + expected +
              "\ngot\n  " + actual);
}

}  // namespace

void x2_structural_bitwise_matches_typescript_contract() {
  expect(0x37, {}, {}, {"hex:12345678:mantissa"}, {"hex:12345678:mantissa"}, "v=8.2345678|disp=mantissa:8.2345678:decimal|ms=hex:8.2345678:mantissa");
  expect(0x38, {}, {}, {"hex:12345678:mantissa"}, {"hex:12345678:mantissa"}, "v=8.2345678|disp=mantissa:8.2345678:decimal|ms=hex:8.2345678:mantissa");
  expect(0x39, {}, {}, {"hex:12345678:mantissa"}, {"hex:12345678:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x37, {}, {}, {"hex:F0000000:mantissa"}, {"hex:0F000000:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"hex:F0000000:mantissa"}, {"hex:0F000000:mantissa"}, "v=_|disp=_|ms=hex:8.F000000:mantissa");
  expect(0x39, {}, {}, {"hex:F0000000:mantissa"}, {"hex:0F000000:mantissa"}, "v=_|disp=_|ms=hex:8.F000000:mantissa");
  expect(0x37, {}, {}, {"hex:FFFFFFFF:mantissa"}, {"hex:0:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"hex:FFFFFFFF:mantissa"}, {"hex:0:mantissa"}, "v=_|disp=_|ms=hex:8.FFFFFFF:mantissa");
  expect(0x39, {}, {}, {"hex:FFFFFFFF:mantissa"}, {"hex:0:mantissa"}, "v=_|disp=_|ms=hex:8.FFFFFFF:mantissa");
  expect(0x37, {}, {}, {"hex:\xD0\x90\xD0\x91\xD0\xA1\xD0\x93:mantissa"}, {"hex:1234:mantissa"}, "v=_|disp=_|ms=_");
  expect(0x38, {}, {}, {"hex:\xD0\x90\xD0\x91\xD0\xA1\xD0\x93:mantissa"}, {"hex:1234:mantissa"}, "v=_|disp=_|ms=_");
  expect(0x39, {}, {}, {"hex:\xD0\x90\xD0\x91\xD0\xA1\xD0\x93:mantissa"}, {"hex:1234:mantissa"}, "v=_|disp=_|ms=_");
  expect(0x37, {}, {}, {"hex:5:mantissa"}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"hex:5:mantissa"}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x39, {}, {}, {"hex:5:mantissa"}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x37, {}, {}, {"hex:12:mantissa"}, {"hex:34:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"hex:12:mantissa"}, {"hex:34:mantissa"}, "v=8.6|disp=mantissa:8.6000000:decimal|ms=hex:8.6000000:mantissa");
  expect(0x39, {}, {}, {"hex:12:mantissa"}, {"hex:34:mantissa"}, "v=8.6|disp=mantissa:8.6000000:decimal|ms=hex:8.6000000:mantissa");
  expect(0x37, {"decimal:5:normalized"}, {}, {}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {"decimal:5:normalized"}, {}, {}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x39, {"decimal:5:normalized"}, {}, {}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x37, {}, {"decimal:12345678:normalized"}, {"hex:F0F0F0F0:mantissa"}, {}, "v=8.030507|disp=mantissa:8.0305070:decimal|ms=hex:8.0305070:mantissa");
  expect(0x38, {}, {"decimal:12345678:normalized"}, {"hex:F0F0F0F0:mantissa"}, {}, "v=_|disp=_|ms=hex:8.2F4F6F8:mantissa");
  expect(0x39, {}, {"decimal:12345678:normalized"}, {"hex:F0F0F0F0:mantissa"}, {}, "v=_|disp=_|ms=hex:8.2C4A688:mantissa");
  expect(0x37, {"decimal:255:normalized"}, {"decimal:15:normalized"}, {}, {}, "v=_|disp=_|ms=_");
  expect(0x38, {"decimal:255:normalized"}, {"decimal:15:normalized"}, {}, {}, "v=_|disp=_|ms=_");
  expect(0x39, {"decimal:255:normalized"}, {"decimal:15:normalized"}, {}, {}, "v=_|disp=_|ms=_");
  expect(0x37, {}, {}, {"hex:CACACACA:mantissa"}, {"hex:0E0E0E0E:mantissa"}, "v=_|disp=_|ms=hex:8.A0A0A0A:mantissa");
  expect(0x38, {}, {}, {"hex:CACACACA:mantissa"}, {"hex:0E0E0E0E:mantissa"}, "v=_|disp=_|ms=hex:8.ECECECE:mantissa");
  expect(0x39, {}, {}, {"hex:CACACACA:mantissa"}, {"hex:0E0E0E0E:mantissa"}, "v=_|disp=_|ms=hex:8.4C4C4C4:mantissa");
  expect(0x37, {}, {}, {"super:FA"}, {"hex:1:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"super:FA"}, {"hex:1:mantissa"}, "v=_|disp=_|ms=hex:8.A000000:mantissa");
  expect(0x39, {}, {}, {"super:FA"}, {"hex:1:mantissa"}, "v=_|disp=_|ms=hex:8.A000000:mantissa");
  expect(0x37, {}, {}, {"hex:1.5:mantissa"}, {"hex:2:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"hex:1.5:mantissa"}, {"hex:2:mantissa"}, "v=8.5|disp=mantissa:8.5000000:decimal|ms=hex:8.5000000:mantissa");
  expect(0x39, {}, {}, {"hex:1.5:mantissa"}, {"hex:2:mantissa"}, "v=8.5|disp=mantissa:8.5000000:decimal|ms=hex:8.5000000:mantissa");
  expect(0x37, {}, {}, {"mantissa:5:decimal"}, {"hex:3:mantissa"}, "v=_|disp=_|ms=_");
  expect(0x38, {}, {}, {"mantissa:5:decimal"}, {"hex:3:mantissa"}, "v=_|disp=_|ms=_");
  expect(0x39, {}, {}, {"mantissa:5:decimal"}, {"hex:3:mantissa"}, "v=_|disp=_|ms=_");
  expect(0x37, {}, {}, {"hex:\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95:mantissa"}, {"hex:11111111:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {}, {}, {"hex:\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95:mantissa"}, {"hex:11111111:mantissa"}, "v=_|disp=_|ms=hex:8.FFFFFFF:mantissa");
  expect(0x39, {}, {}, {"hex:\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95\xD0\x95:mantissa"}, {"hex:11111111:mantissa"}, "v=_|disp=_|ms=hex:8.FFFFFFF:mantissa");
  expect(0x37, {"decimal:9:normalized"}, {}, {"hex:6:mantissa"}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x38, {"decimal:9:normalized"}, {}, {"hex:6:mantissa"}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
  expect(0x39, {"decimal:9:normalized"}, {}, {"hex:6:mantissa"}, {"hex:3:mantissa"}, "v=8|disp=mantissa:8.0000000:decimal|ms=hex:8.0000000:mantissa");
}

}  // namespace mkpro::tests
