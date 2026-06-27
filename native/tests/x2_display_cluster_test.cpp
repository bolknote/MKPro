#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::x2_display_cluster_debug;
using mkpro::core::passes::x2_decimal_fraction_part_shape_debug;

}  // namespace

void x2_display_cluster_matches_typescript_contract() {
  const std::vector<std::pair<std::string, std::string>> shape_cases = {
      {"mantissa:1.5:decimal", "rvd=1.5|sso=_|edd=1.5|ennd=1.5|dds=mantissa:1.5:decimal|ssrvd=1.5"},
      {"mantissa:-0:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:01.20:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:000:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:-12.30:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:0.5:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:00012:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:-0.0:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:123456789:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"exponent:1.5:3:decimal", "rvd=1500|sso=_|edd=1500|ennd=1500|dds=mantissa:1500:decimal|ssrvd=1500"},
      {"exponent:2:-5:decimal", "rvd=0.00002|sso=_|edd=0.00002|ennd=0.00002|dds=exponent:2:-5:decimal|ssrvd=0.00002"},
      {"exponent:1:99:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"exponent:123:0:decimal", "rvd=123|sso=_|edd=123|ennd=123|dds=mantissa:123:decimal|ssrvd=123"},
      {"exponent:9.9:9:decimal", "rvd=9900000000|sso=_|edd=9900000000|ennd=9900000000|dds=exponent:9.9:9:decimal|ssrvd=9900000000"},
      {"hex:1A.B:mantissa", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"hex:1.234:mantissa", "rvd=1.234|sso=1.234|edd=1.234|ennd=1.234|dds=_|ssrvd=1.234"},
      {"hex:1С.Г:mantissa", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"hex:0C:mantissa", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"hex:5:mantissa", "rvd=5|sso=5|edd=5|ennd=5|dds=_|ssrvd=5"},
      {"hex:-5:mantissa", "rvd=-5|sso=-5|edd=-5|ennd=_|dds=_|ssrvd=-5"},
      {"hex:12:mantissa", "rvd=12|sso=12|edd=12|ennd=12|dds=_|ssrvd=12"},
      {"hex:1.2:mantissa", "rvd=1.2|sso=1.2|edd=1.2|ennd=1.2|dds=_|ssrvd=1.2"},
      {"hex-exponent:1A:3", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"hex-exponent:CF:-2", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"hex-exponent:5:1", "rvd=50|sso=50|edd=50|ennd=50|dds=_|ssrvd=50"},
      {"super:FA", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"super:-FE", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"super-exponent:FA:3", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"super-exponent:FE:-7", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"mantissa:1.2.3:decimal", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
      {"garbage:x:y", "rvd=_|sso=_|edd=_|ennd=_|dds=_|ssrvd=_"},
  };
  for (const auto& entry : shape_cases) {
    const std::string actual = x2_display_cluster_debug(entry.first);
    require(actual == entry.second,
            "display cluster for [" + entry.first + "] expected\n  " + entry.second + "\ngot\n  " +
                actual);
  }
  const std::vector<std::pair<std::string, std::string>> fraction_cases = {
      {"3.25", "exponent:2.5:-1:decimal"},
      {"-3.25", "exponent:-2.5:-1:decimal"},
      {"0.5", "exponent:5:-1:decimal"},
      {"-0.005", "exponent:-5:-3:decimal"},
      {"100", "mantissa:0:decimal"},
      {"5", "mantissa:0:decimal"},
      {"0.000", "mantissa:0:decimal"},
      {"1.20", "exponent:2:-1:decimal"},
      {"-0.25", "exponent:-2.5:-1:decimal"},
      {"12.0050", "exponent:5:-3:decimal"},
      {"0", "mantissa:0:decimal"},
      {"-0", "mantissa:-0:decimal"},
  };
  for (const auto& entry : fraction_cases) {
    const std::string actual = x2_decimal_fraction_part_shape_debug(entry.first);
    require(actual == entry.second,
            "fraction-part shape for [" + entry.first + "] expected " + entry.second + " got " +
                actual);
  }
}

}  // namespace mkpro::tests
