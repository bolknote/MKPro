#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::x2_shape_data_model_debug;

}  // namespace

void x2_shape_data_model_matches_typescript_contract() {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"mantissa:1.5:decimal", "mantissa|radix=decimal|canon=1.5|sign=|dp=1|lz=0|digits=15|sig=2|norm=1.5|same=1|safety=dotSafeDecimal|canonFact=mantissa:1.5:decimal|rt=mantissa:1.5:decimal"},
      {"mantissa:-0:decimal", "mantissa|radix=decimal|canon=-0|sign=-|dp=0|lz=0|digits=0|sig=1|norm=0|same=0|safety=errorProne|canonFact=mantissa:-0:decimal|rt=mantissa:-0:decimal"},
      {"mantissa:01.20:decimal", "mantissa|radix=decimal|canon=01.20|sign=|dp=1|lz=1|digits=0120|sig=2|norm=1.2|same=0|safety=dotSafeDecimal|canonFact=mantissa:01.20:decimal|rt=mantissa:01.20:decimal"},
      {"mantissa:.5:decimal", "unknown|safety=unknown|canonFact=mantissa:.5:decimal|rt=_"},
      {"mantissa:1,5:decimal", "mantissa|radix=decimal|canon=1.5|sign=|dp=1|lz=0|digits=15|sig=2|norm=1.5|same=1|safety=dotSafeDecimal|canonFact=mantissa:1.5:decimal|rt=mantissa:1.5:decimal"},
      {"mantissa:000:decimal", "mantissa|radix=decimal|canon=000|sign=|dp=0|lz=1|digits=000|sig=1|norm=0|same=0|safety=dotSafeDecimal|canonFact=mantissa:000:decimal|rt=mantissa:000:decimal"},
      {"mantissa:123456789:decimal", "unknown|safety=unknown|canonFact=mantissa:123456789:decimal|rt=_"},
      {"mantissa:-12.30:decimal", "mantissa|radix=decimal|canon=-12.30|sign=-|dp=1|lz=0|digits=1230|sig=3|norm=-12.3|same=0|safety=dotSafeDecimal|canonFact=mantissa:-12.30:decimal|rt=mantissa:-12.30:decimal"},
      {"mantissa:0.5:decimal", "mantissa|radix=decimal|canon=0.5|sign=|dp=1|lz=0|digits=05|sig=1|norm=0.5|same=1|safety=dotSafeDecimal|canonFact=mantissa:0.5:decimal|rt=mantissa:0.5:decimal"},
      {"mantissa:00012:decimal", "mantissa|radix=decimal|canon=00012|sign=|dp=0|lz=1|digits=00012|sig=2|norm=12|same=0|safety=dotSafeDecimal|canonFact=mantissa:00012:decimal|rt=mantissa:00012:decimal"},
      {"mantissa:-0.0:decimal", "mantissa|radix=decimal|canon=-0.0|sign=-|dp=1|lz=0|digits=00|sig=1|norm=0|same=0|safety=errorProne|canonFact=mantissa:-0.0:decimal|rt=mantissa:-0.0:decimal"},
      {"exponent:1.5:3:decimal", "exp|mradix=decimal|mcanon=1.5|eraw=3|esign=|edigits=3|norm=1500|closedDecDisp=mantissa:1500:decimal|closedStruct=_|closedExp=mantissa:1500:decimal|closedDecExp=mantissa:1500:decimal|closedStructM=_|safety=errorProne|canonFact=exponent:1.5:3:decimal|rt=exponent:1.5:3:decimal"},
      {"exponent:2:-5:decimal", "exp|mradix=decimal|mcanon=2|eraw=-5|esign=-|edigits=5|norm=0.00002|closedDecDisp=exponent:2:-5:decimal|closedStruct=_|closedExp=exponent:2:-5:decimal|closedDecExp=exponent:2:-5:decimal|closedStructM=_|safety=errorProne|canonFact=exponent:2:-5:decimal|rt=exponent:2:-5:decimal"},
      {"exponent:1:99:decimal", "exp|mradix=decimal|mcanon=1|eraw=99|esign=|edigits=99|norm=_|closedDecDisp=_|closedStruct=_|closedExp=_|closedDecExp=_|closedStructM=_|safety=errorProne|canonFact=exponent:1:99:decimal|rt=exponent:1:99:decimal"},
      {"exponent:1:100:decimal", "unknown|safety=unknown|canonFact=exponent:1:100:decimal|rt=_"},
      {"exponent:123:0:decimal", "exp|mradix=decimal|mcanon=123|eraw=0|esign=|edigits=0|norm=123|closedDecDisp=mantissa:123:decimal|closedStruct=_|closedExp=mantissa:123:decimal|closedDecExp=mantissa:123:decimal|closedStructM=_|safety=errorProne|canonFact=exponent:123:0:decimal|rt=exponent:123:0:decimal"},
      {"exponent:9.9:9:decimal", "exp|mradix=decimal|mcanon=9.9|eraw=9|esign=|edigits=9|norm=9900000000|closedDecDisp=exponent:9.9:9:decimal|closedStruct=_|closedExp=exponent:9.9:9:decimal|closedDecExp=exponent:9.9:9:decimal|closedStructM=_|safety=errorProne|canonFact=exponent:9.9:9:decimal|rt=exponent:9.9:9:decimal"},
      {"hex:1A.B:mantissa", "mantissa|radix=hex|canon=1A.B|sign=|dp=1|lz=0|digits=1AB|sig=3|norm=_|same=0|safety=structuralOnly|canonFact=hex:1A.B:mantissa|rt=hex:1A.B:mantissa"},
      {"hex:-CF.2:mantissa", "mantissa|radix=hex|canon=-CF.2|sign=-|dp=1|lz=0|digits=CF2|sig=3|norm=_|same=0|safety=structuralOnly|canonFact=hex:-CF.2:mantissa|rt=hex:-CF.2:mantissa"},
      {"hex:1.234:mantissa", "mantissa|radix=hex|canon=1.234|sign=|dp=1|lz=0|digits=1234|sig=4|norm=1.234|same=1|safety=structuralOnly|canonFact=hex:1.234:mantissa|rt=hex:1.234:mantissa"},
      {"hex:a.f:mantissa", "mantissa|radix=hex|canon=A.F|sign=|dp=1|lz=0|digits=AF|sig=2|norm=_|same=0|safety=structuralOnly|canonFact=hex:A.F:mantissa|rt=hex:A.F:mantissa"},
      {"hex:0C:mantissa", "mantissa|radix=hex|canon=0C|sign=|dp=0|lz=1|digits=0C|sig=1|norm=_|same=0|safety=structuralOnly|canonFact=hex:0C:mantissa|rt=hex:0C:mantissa"},
      {"hex:1С.Г:mantissa", "mantissa|radix=hex|canon=1С.Г|sign=|dp=1|lz=0|digits=1СГ|sig=3|norm=_|same=0|safety=structuralOnly|canonFact=hex:1С.Г:mantissa|rt=hex:1С.Г:mantissa"},
      {"hex:1Е:mantissa", "mantissa|radix=hex|canon=1Е|sign=|dp=0|lz=0|digits=1Е|sig=2|norm=_|same=0|safety=structuralOnly|canonFact=hex:1Е:mantissa|rt=hex:1Е:mantissa"},
      {"hex:с:mantissa", "mantissa|radix=hex|canon=С|sign=|dp=0|lz=0|digits=С|sig=1|norm=_|same=0|safety=structuralOnly|canonFact=hex:С:mantissa|rt=hex:С:mantissa"},
      {"hex:12.3.4:mantissa", "unknown|safety=unknown|canonFact=hex:12.3.4:mantissa|rt=_"},
      {"hex-exponent:1A:3", "exp|mradix=hex|mcanon=1A|eraw=3|esign=|edigits=3|norm=_|closedDecDisp=_|closedStruct=1A000|closedExp=hex:1A000:mantissa|closedDecExp=_|closedStructM=hex:1A000:mantissa|safety=structuralOnly|canonFact=hex-exponent:1A:3|rt=hex-exponent:1A:3"},
      {"hex-exponent:CF:-2", "exp|mradix=hex|mcanon=CF|eraw=-2|esign=-|edigits=2|norm=_|closedDecDisp=_|closedStruct=0.CF|closedExp=hex:0.CF:mantissa|closedDecExp=_|closedStructM=hex:0.CF:mantissa|safety=structuralOnly|canonFact=hex-exponent:CF:-2|rt=hex-exponent:CF:-2"},
      {"hex-exponent:Е:3", "exp|mradix=hex|mcanon=Е|eraw=3|esign=|edigits=3|norm=_|closedDecDisp=_|closedStruct=Е000|closedExp=hex:Е000:mantissa|closedDecExp=_|closedStructM=hex:Е000:mantissa|safety=structuralOnly|canonFact=hex-exponent:Е:3|rt=hex-exponent:Е:3"},
      {"hex-exponent:1A:0", "exp|mradix=hex|mcanon=1A|eraw=0|esign=|edigits=0|norm=_|closedDecDisp=_|closedStruct=1A|closedExp=hex:1A:mantissa|closedDecExp=_|closedStructM=hex:1A:mantissa|safety=structuralOnly|canonFact=hex-exponent:1A:0|rt=hex-exponent:1A:0"},
      {"super:FA:", "unknown|safety=unknown|canonFact=super:FA:|rt=_"},
      {"super:-FE:", "unknown|safety=unknown|canonFact=super:-FE:|rt=_"},
      {"super:FC:", "unknown|safety=unknown|canonFact=super:FC:|rt=_"},
      {"super:FG:", "unknown|safety=unknown|canonFact=super:FG:|rt=_"},
      {"super:FA", "mantissa|radix=super|canon=FA|sign=|dp=0|lz=0|digits=FA|sig=2|norm=_|same=0|safety=structuralOnly|canonFact=super:FA|rt=super:FA"},
      {"super:-FE", "mantissa|radix=super|canon=-FE|sign=-|dp=0|lz=0|digits=FE|sig=2|norm=_|same=0|safety=structuralOnly|canonFact=super:-FE|rt=super:-FE"},
      {"super:FC", "mantissa|radix=super|canon=FC|sign=|dp=0|lz=0|digits=FC|sig=2|norm=_|same=0|safety=structuralOnly|canonFact=super:FC|rt=super:FC"},
      {"super:FG", "unknown|safety=unknown|canonFact=super:FG|rt=_"},
      {"super-exponent:FA:3", "exp|mradix=super|mcanon=FA|eraw=3|esign=|edigits=3|norm=_|closedDecDisp=_|closedStruct=FA000|closedExp=hex:FA000:mantissa|closedDecExp=_|closedStructM=hex:FA000:mantissa|safety=structuralOnly|canonFact=super-exponent:FA:3|rt=super-exponent:FA:3"},
      {"super-exponent:FE:-7", "exp|mradix=super|mcanon=FE|eraw=-7|esign=-|edigits=7|norm=_|closedDecDisp=_|closedStruct=0.00000FE|closedExp=hex:0.00000FE:mantissa|closedDecExp=_|closedStructM=hex:0.00000FE:mantissa|safety=structuralOnly|canonFact=super-exponent:FE:-7|rt=super-exponent:FE:-7"},
      {"super-exponent:FA:8", "exp|mradix=super|mcanon=FA|eraw=8|esign=|edigits=8|norm=_|closedDecDisp=_|closedStruct=_|closedExp=_|closedDecExp=_|closedStructM=_|safety=structuralOnly|canonFact=super-exponent:FA:8|rt=super-exponent:FA:8"},
      {"mantissa:1.2.3:decimal", "unknown|safety=unknown|canonFact=mantissa:1.2.3:decimal|rt=_"},
      {"garbage:x:y", "unknown|safety=unknown|canonFact=garbage:x:y|rt=_"},
  };
  for (const auto& entry : cases) {
    const std::string actual = x2_shape_data_model_debug(entry.first);
    require(actual == entry.second,
            "shape model for [" + entry.first + "] expected\n  " + entry.second + "\ngot\n  " +
                actual);
  }
}

}  // namespace mkpro::tests
