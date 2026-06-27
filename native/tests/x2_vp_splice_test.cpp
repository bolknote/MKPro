#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <string>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::x2_vp_splice_debug;

void expect(const std::string& x_shape, const std::string& x2_shape, bool include_exponent_targets,
            const std::string& expected) {
  const std::string actual = x2_vp_splice_debug(x_shape, x2_shape, include_exponent_targets);
  require(actual == expected, "vp splice xShape=" + x_shape + " x2Shape=" + x2_shape +
                                  " incl=" + (include_exponent_targets ? "1" : "0") +
                                  " expected\n  " + expected + "\ngot\n  " + actual);
}

}  // namespace

void x2_vp_splice_matches_typescript_contract() {
  expect("mantissa:5:decimal", "mantissa:25:decimal", false, "src=mantissa:5:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=55");
  expect("mantissa:5:decimal", "mantissa:25:decimal", true, "src=mantissa:5:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=55");
  expect("mantissa:5:decimal", "hex:5:mantissa", false, "src=mantissa:5:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:5:mantissa|dmant=_");
  expect("mantissa:5:decimal", "hex:5:mantissa", true, "src=mantissa:5:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:5:mantissa|dmant=_");
  expect("mantissa:5:decimal", "exponent:3:2:decimal", false, "src=mantissa:5:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=500");
  expect("mantissa:5:decimal", "exponent:3:2:decimal", true, "src=mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=5,500");
  expect("mantissa:5:decimal", "exponent:300:2:decimal", false, "src=mantissa:5:decimal|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=50000");
  expect("mantissa:5:decimal", "exponent:300:2:decimal", true, "src=mantissa:5:decimal|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=500,50000");
  expect("mantissa:5:decimal", "mantissa:300:decimal", false, "src=mantissa:5:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=500");
  expect("mantissa:5:decimal", "mantissa:300:decimal", true, "src=mantissa:5:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=500");
  expect("mantissa:5:decimal", "hex:8.FFFFFFF:mantissa", false, "src=mantissa:5:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:5.FFFFFFF:mantissa|dmant=_");
  expect("mantissa:5:decimal", "hex:8.FFFFFFF:mantissa", true, "src=mantissa:5:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:5.FFFFFFF:mantissa|dmant=_");
  expect("mantissa:5:decimal", "mantissa:12345678:decimal", false, "src=mantissa:5:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=52345678");
  expect("mantissa:5:decimal", "mantissa:12345678:decimal", true, "src=mantissa:5:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=52345678");
  expect("mantissa:5:decimal", "_", false, "src=mantissa:5:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("mantissa:5:decimal", "_", true, "src=mantissa:5:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("hex:A:mantissa", "mantissa:25:decimal", false, "src=hex:A:mantissa|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=hex:A5:mantissa|dmant=_");
  expect("hex:A:mantissa", "mantissa:25:decimal", true, "src=hex:A:mantissa|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=hex:A5:mantissa|dmant=_");
  expect("hex:A:mantissa", "hex:5:mantissa", false, "src=hex:A:mantissa|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:A:mantissa|dmant=_");
  expect("hex:A:mantissa", "hex:5:mantissa", true, "src=hex:A:mantissa|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:A:mantissa|dmant=_");
  expect("hex:A:mantissa", "exponent:3:2:decimal", false, "src=hex:A:mantissa|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=hex:A00:mantissa|dmant=_");
  expect("hex:A:mantissa", "exponent:3:2:decimal", true, "src=hex:A:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=_");
  expect("hex:A:mantissa", "exponent:300:2:decimal", false, "src=hex:A:mantissa|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=hex:A0000:mantissa|dmant=_");
  expect("hex:A:mantissa", "exponent:300:2:decimal", true, "src=hex:A:mantissa|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=hex:A0000:mantissa,hex:A00:mantissa|dmant=_");
  expect("hex:A:mantissa", "mantissa:300:decimal", false, "src=hex:A:mantissa|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=hex:A00:mantissa|dmant=_");
  expect("hex:A:mantissa", "mantissa:300:decimal", true, "src=hex:A:mantissa|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=hex:A00:mantissa|dmant=_");
  expect("hex:A:mantissa", "hex:8.FFFFFFF:mantissa", false, "src=hex:A:mantissa|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:A.FFFFFFF:mantissa|dmant=_");
  expect("hex:A:mantissa", "hex:8.FFFFFFF:mantissa", true, "src=hex:A:mantissa|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:A.FFFFFFF:mantissa|dmant=_");
  expect("hex:A:mantissa", "mantissa:12345678:decimal", false, "src=hex:A:mantissa|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=hex:A2345678:mantissa|dmant=_");
  expect("hex:A:mantissa", "mantissa:12345678:decimal", true, "src=hex:A:mantissa|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=hex:A2345678:mantissa|dmant=_");
  expect("hex:A:mantissa", "_", false, "src=hex:A:mantissa|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("hex:A:mantissa", "_", true, "src=hex:A:mantissa|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:25:decimal", false, "src=_|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:25:decimal", true, "src=_|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "hex:5:mantissa", false, "src=_|dtgt=_|vtgt=hex:5:mantissa|sshape=_|dmant=_");
  expect("super:5:mantissa", "hex:5:mantissa", true, "src=_|dtgt=_|vtgt=hex:5:mantissa|sshape=_|dmant=_");
  expect("super:5:mantissa", "exponent:3:2:decimal", false, "src=_|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "exponent:3:2:decimal", true, "src=_|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "exponent:300:2:decimal", false, "src=_|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "exponent:300:2:decimal", true, "src=_|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:300:decimal", false, "src=_|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:300:decimal", true, "src=_|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "hex:8.FFFFFFF:mantissa", false, "src=_|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=_|dmant=_");
  expect("super:5:mantissa", "hex:8.FFFFFFF:mantissa", true, "src=_|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:12345678:decimal", false, "src=_|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:12345678:decimal", true, "src=_|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "_", false, "src=_|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("super:5:mantissa", "_", true, "src=_|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("exponent:3:2:decimal", "mantissa:25:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=35");
  expect("exponent:3:2:decimal", "mantissa:25:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=35");
  expect("exponent:3:2:decimal", "hex:5:mantissa", false, "src=mantissa:300:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:3:mantissa|dmant=_");
  expect("exponent:3:2:decimal", "hex:5:mantissa", true, "src=mantissa:300:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:3:mantissa|dmant=_");
  expect("exponent:3:2:decimal", "exponent:3:2:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=300");
  expect("exponent:3:2:decimal", "exponent:3:2:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=3,300");
  expect("exponent:3:2:decimal", "exponent:300:2:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=30000");
  expect("exponent:3:2:decimal", "exponent:300:2:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=300,30000");
  expect("exponent:3:2:decimal", "mantissa:300:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=300");
  expect("exponent:3:2:decimal", "mantissa:300:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=300");
  expect("exponent:3:2:decimal", "hex:8.FFFFFFF:mantissa", false, "src=mantissa:300:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:3.FFFFFFF:mantissa|dmant=_");
  expect("exponent:3:2:decimal", "hex:8.FFFFFFF:mantissa", true, "src=mantissa:300:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:3.FFFFFFF:mantissa|dmant=_");
  expect("exponent:3:2:decimal", "mantissa:12345678:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=32345678");
  expect("exponent:3:2:decimal", "mantissa:12345678:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=32345678");
  expect("exponent:3:2:decimal", "_", false, "src=mantissa:300:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("exponent:3:2:decimal", "_", true, "src=mantissa:300:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("mantissa:-5:decimal", "mantissa:25:decimal", false, "src=mantissa:-5:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=55");
  expect("mantissa:-5:decimal", "mantissa:25:decimal", true, "src=mantissa:-5:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=55");
  expect("mantissa:-5:decimal", "hex:5:mantissa", false, "src=mantissa:-5:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=_|dmant=_");
  expect("mantissa:-5:decimal", "hex:5:mantissa", true, "src=mantissa:-5:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=_|dmant=_");
  expect("mantissa:-5:decimal", "exponent:3:2:decimal", false, "src=mantissa:-5:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=500");
  expect("mantissa:-5:decimal", "exponent:3:2:decimal", true, "src=mantissa:-5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=5,500");
  expect("mantissa:-5:decimal", "exponent:300:2:decimal", false, "src=mantissa:-5:decimal|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=50000");
  expect("mantissa:-5:decimal", "exponent:300:2:decimal", true, "src=mantissa:-5:decimal|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=500,50000");
  expect("mantissa:-5:decimal", "mantissa:300:decimal", false, "src=mantissa:-5:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=500");
  expect("mantissa:-5:decimal", "mantissa:300:decimal", true, "src=mantissa:-5:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=500");
  expect("mantissa:-5:decimal", "hex:8.FFFFFFF:mantissa", false, "src=mantissa:-5:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=_|dmant=_");
  expect("mantissa:-5:decimal", "hex:8.FFFFFFF:mantissa", true, "src=mantissa:-5:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=_|dmant=_");
  expect("mantissa:-5:decimal", "mantissa:12345678:decimal", false, "src=mantissa:-5:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=52345678");
  expect("mantissa:-5:decimal", "mantissa:12345678:decimal", true, "src=mantissa:-5:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=52345678");
  expect("mantissa:-5:decimal", "_", false, "src=mantissa:-5:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("mantissa:-5:decimal", "_", true, "src=mantissa:-5:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "mantissa:25:decimal", false, "src=mantissa:0:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "mantissa:25:decimal", true, "src=mantissa:0:decimal|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "hex:5:mantissa", false, "src=mantissa:0:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:0:mantissa|dmant=_");
  expect("mantissa:0:decimal", "hex:5:mantissa", true, "src=mantissa:0:decimal|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:0:mantissa|dmant=_");
  expect("mantissa:0:decimal", "exponent:3:2:decimal", false, "src=mantissa:0:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "exponent:3:2:decimal", true, "src=mantissa:0:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "exponent:300:2:decimal", false, "src=mantissa:0:decimal|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "exponent:300:2:decimal", true, "src=mantissa:0:decimal|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "mantissa:300:decimal", false, "src=mantissa:0:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "mantissa:300:decimal", true, "src=mantissa:0:decimal|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "hex:8.FFFFFFF:mantissa", false, "src=mantissa:0:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:0.FFFFFFF:mantissa|dmant=_");
  expect("mantissa:0:decimal", "hex:8.FFFFFFF:mantissa", true, "src=mantissa:0:decimal|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:0.FFFFFFF:mantissa|dmant=_");
  expect("mantissa:0:decimal", "mantissa:12345678:decimal", false, "src=mantissa:0:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "mantissa:12345678:decimal", true, "src=mantissa:0:decimal|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "_", false, "src=mantissa:0:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "_", true, "src=mantissa:0:decimal|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("hex:1A:mantissa", "mantissa:25:decimal", false, "src=hex:1A:mantissa|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=15");
  expect("hex:1A:mantissa", "mantissa:25:decimal", true, "src=hex:1A:mantissa|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=15");
  expect("hex:1A:mantissa", "hex:5:mantissa", false, "src=hex:1A:mantissa|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:1:mantissa|dmant=_");
  expect("hex:1A:mantissa", "hex:5:mantissa", true, "src=hex:1A:mantissa|dtgt=_|vtgt=hex:5:mantissa|sshape=hex:1:mantissa|dmant=_");
  expect("hex:1A:mantissa", "exponent:3:2:decimal", false, "src=hex:1A:mantissa|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=100");
  expect("hex:1A:mantissa", "exponent:3:2:decimal", true, "src=hex:1A:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=1,100");
  expect("hex:1A:mantissa", "exponent:300:2:decimal", false, "src=hex:1A:mantissa|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=10000");
  expect("hex:1A:mantissa", "exponent:300:2:decimal", true, "src=hex:1A:mantissa|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=100,10000");
  expect("hex:1A:mantissa", "mantissa:300:decimal", false, "src=hex:1A:mantissa|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=100");
  expect("hex:1A:mantissa", "mantissa:300:decimal", true, "src=hex:1A:mantissa|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=100");
  expect("hex:1A:mantissa", "hex:8.FFFFFFF:mantissa", false, "src=hex:1A:mantissa|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:1.FFFFFFF:mantissa|dmant=_");
  expect("hex:1A:mantissa", "hex:8.FFFFFFF:mantissa", true, "src=hex:1A:mantissa|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=hex:1.FFFFFFF:mantissa|dmant=_");
  expect("hex:1A:mantissa", "mantissa:12345678:decimal", false, "src=hex:1A:mantissa|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=12345678");
  expect("hex:1A:mantissa", "mantissa:12345678:decimal", true, "src=hex:1A:mantissa|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=12345678");
  expect("hex:1A:mantissa", "_", false, "src=hex:1A:mantissa|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("hex:1A:mantissa", "_", true, "src=hex:1A:mantissa|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("_", "mantissa:25:decimal", false, "src=_|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=_");
  expect("_", "mantissa:25:decimal", true, "src=_|dtgt=mantissa:25:decimal|vtgt=mantissa:25:decimal|sshape=_|dmant=_");
  expect("_", "hex:5:mantissa", false, "src=_|dtgt=_|vtgt=hex:5:mantissa|sshape=_|dmant=_");
  expect("_", "hex:5:mantissa", true, "src=_|dtgt=_|vtgt=hex:5:mantissa|sshape=_|dmant=_");
  expect("_", "exponent:3:2:decimal", false, "src=_|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("_", "exponent:3:2:decimal", true, "src=_|dtgt=mantissa:300:decimal,mantissa:3:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal|sshape=_|dmant=_");
  expect("_", "exponent:300:2:decimal", false, "src=_|dtgt=mantissa:30000:decimal|vtgt=mantissa:30000:decimal|sshape=_|dmant=_");
  expect("_", "exponent:300:2:decimal", true, "src=_|dtgt=mantissa:30000:decimal,mantissa:300:decimal|vtgt=mantissa:30000:decimal,mantissa:300:decimal|sshape=_|dmant=_");
  expect("_", "mantissa:300:decimal", false, "src=_|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("_", "mantissa:300:decimal", true, "src=_|dtgt=mantissa:300:decimal|vtgt=mantissa:300:decimal|sshape=_|dmant=_");
  expect("_", "hex:8.FFFFFFF:mantissa", false, "src=_|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=_|dmant=_");
  expect("_", "hex:8.FFFFFFF:mantissa", true, "src=_|dtgt=_|vtgt=hex:8.FFFFFFF:mantissa|sshape=_|dmant=_");
  expect("_", "mantissa:12345678:decimal", false, "src=_|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=_");
  expect("_", "mantissa:12345678:decimal", true, "src=_|dtgt=mantissa:12345678:decimal|vtgt=mantissa:12345678:decimal|sshape=_|dmant=_");
  expect("_", "_", false, "src=_|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("_", "_", true, "src=_|dtgt=_|vtgt=_|sshape=_|dmant=_");
  expect("mantissa:5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=5,500");
  expect("mantissa:5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=5,500");
  expect("mantissa:25:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:25:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=2,200");
  expect("mantissa:25:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:25:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=2,200");
  expect("mantissa:-5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:-5:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=5,500");
  expect("mantissa:-5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:-5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=5,500");
  expect("mantissa:0:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:0:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("mantissa:0:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:0:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("mantissa:1.5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:1.5:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=1,100");
  expect("mantissa:1.5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:1.5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=1,100");
  expect("mantissa:300:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("mantissa:300:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("mantissa:0.5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=_|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("mantissa:0.5:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=_|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("mantissa:12345678:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:12345678:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=1,100");
  expect("mantissa:12345678:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:12345678:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=1,100");
  expect("mantissa:99999999:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:99999999:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=9,900");
  expect("mantissa:99999999:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:99999999:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=9,900");
  expect("mantissa:-0:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=_|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("mantissa:-0:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=_|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("hex:A:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:A:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=_");
  expect("hex:A:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:A:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=_");
  expect("hex:5:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:5:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=5,500");
  expect("hex:5:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:5:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=5,500");
  expect("hex:1A:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:1A:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=1,100");
  expect("hex:1A:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:1A:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=1,100");
  expect("hex:F:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:F:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=hex:F00:mantissa,hex:F:mantissa|dmant=_");
  expect("hex:F:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:F:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=hex:F00:mantissa,hex:F:mantissa|dmant=_");
  expect("hex:8.FFFFFFF:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:8.FFFFFFF:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=8,800");
  expect("hex:8.FFFFFFF:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:8.FFFFFFF:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=8,800");
  expect("hex:C:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:C:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=hex:C00:mantissa,hex:C:mantissa|dmant=_");
  expect("hex:C:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:C:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=hex:C00:mantissa,hex:C:mantissa|dmant=_");
  expect("super:5:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=_|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("super:5:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=_|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("exponent:3:2:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("exponent:3:2:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("exponent:3:-2:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=exponent:3:-2:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("exponent:3:-2:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=exponent:3:-2:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("exponent:300:2:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:30000:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("exponent:300:2:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:30000:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("mantissa:5:decimal|mantissa:25:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:25:decimal,mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=2,200,5,500");
  expect("mantissa:5:decimal|mantissa:25:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:25:decimal,mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=2,200,5,500");
  expect("hex:A:mantissa|hex:5:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:5:mantissa,hex:A:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=5,500");
  expect("hex:A:mantissa|hex:5:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:5:mantissa,hex:A:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=5,500");
  expect("mantissa:5:decimal|hex:A:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:A:mantissa,mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=5,500");
  expect("mantissa:5:decimal|hex:A:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:A:mantissa,mantissa:5:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=hex:A00:mantissa,hex:A:mantissa|dmant=5,500");
  expect("exponent:3:2:decimal|mantissa:300:decimal", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("exponent:3:2:decimal|mantissa:300:decimal", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=mantissa:300:decimal|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=3,300");
  expect("hex:1A:mantissa|hex:8.FFFFFFF:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=hex:1A:mantissa,hex:8.FFFFFFF:mantissa|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=1,100,8,800");
  expect("hex:1A:mantissa|hex:8.FFFFFFF:mantissa", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=hex:1A:mantissa,hex:8.FFFFFFF:mantissa|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=1,100,8,800");
  expect("_", "mantissa:5:decimal|exponent:3:2:decimal", false, "src=_|dtgt=mantissa:300:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:5:decimal|sshape=_|dmant=_");
  expect("_", "mantissa:5:decimal|exponent:3:2:decimal", true, "src=_|dtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|vtgt=mantissa:300:decimal,mantissa:3:decimal,mantissa:5:decimal|sshape=_|dmant=_");
}

}  // namespace mkpro::tests
