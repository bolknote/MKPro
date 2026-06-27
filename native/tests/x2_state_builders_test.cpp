#include "mkpro/core/passes/helpers.hpp"

#include "test_support.hpp"

#include <string>

namespace mkpro::tests {

namespace {

using mkpro::core::passes::x2_state_builders_debug;

void expect(const std::string& scenario, const std::string& a, const std::string& b,
            const std::string& c, const std::string& expected) {
  const std::string actual = x2_state_builders_debug(scenario, a, b, c);
  require(actual == expected, "state builder scenario=" + scenario + " a=" + a + " b=" + b +
                                  " c=" + c + " expected\n  " + expected + "\ngot\n  " + actual);
}

}  // namespace

void x2_state_builders_matches_typescript_contract() {
  expect("openRaw", "1.5", "", "", "open{1.5}");
  expect("openRaw", "-2.50", "", "", "open{-2.50}");
  expect("openRaw", "abc", "", "", "unknown");
  expect("openRaw", "1,2.5", "", "", "open{1,2.5}");
  expect("expParts", "3", "2", "", "exp{3};{2}");
  expect("expParts", "3.14", "-5", "", "exp{3.14};{-5}");
  expect("expParts", "9", "99", "", "exp{9};{99}");
  expect("expParts", "x", "2", "", "unknown");
  expect("expParts", "3", "_", "", "unknown");
  expect("structParts", "hex:5:mantissa", "2", "", "exp{hex:5:mantissa};{2}");
  expect("structParts", "hex:A:mantissa", "-3", "", "exp{hex:A:mantissa};{-3}");
  expect("structParts", "bad", "2", "", "unknown");
  expect("vpParts", "3", "2", "", "exp{3};{2}");
  expect("vpParts", "bad", "2", "", "unknown");
  expect("advDigitClosed", "", "5", "", "open{5}");
  expect("advDigitOpen", "1.5", "2", "", "open{1.52}");
  expect("advDigitOpen", "12345678", "9", "", "unknown");
  expect("advDigitExp", "3", "2", "5", "exp{3};{25}");
  expect("advPoint", "15", "", "", "open{15.}");
  expect("advPoint", "1.5", "", "", "open{1.5}");
  expect("advExpDigit", "3", "2", "5", "exp{3};{25}");
  expect("advExpDigit", "3", "12", "5", "unknown");
  expect("advStructExpDigit", "hex:5:mantissa", "2", "3", "exp{hex:5:mantissa};{23}");
  expect("signExp", "3", "2", "", "exp{3};{-2}");
  expect("signExp", "3", "-2", "", "exp{3};{2}");
  expect("signStructExp", "hex:5:mantissa", "2", "", "exp{hex:5:mantissa};{-2}");
  expect("signVp", "3", "2", "", "exp{3};{-2}");
  expect("closedVals", "3", "2", "", "{decimal:300:normalized}");
  expect("closedVals", "2.5", "-1", "", "{decimal:0.25:normalized}");
  expect("closedShapes", "2.5", "-1", "", "{exponent:2.5:-1:decimal}");
  expect("expShapes", "3", "2", "", "{exponent:3:2:decimal}");
  expect("structExpShapes", "hex:5:mantissa", "2", "", "{hex-exponent:5:2}");
  expect("signedDecimalEntry", "3", "", "", "-3");
  expect("signedDecimalEntry", "-3", "", "", "3");
  expect("signedDecimalEntry", "0", "", "", "-0");
  expect("signedDecimalEntry", "bad", "", "", "0");
  expect("signedMantissaShapes", "3,5", "", "", "{-3,-5}");
  expect("signedMantissaShapes", "0", "", "", "{-0}");
  expect("signedMantissaShapes", "bad", "", "", "_");
  expect("structFromVpShapes", "hex:5:mantissa", "", "", "exp{hex:5:mantissa};{}");
  expect("stateOpenDecimal", "1.5", "", "", "x={decimal:1.5:normalized}|y={}|x2={decimal:1.5:normalized}|xs={mantissa:1.5:decimal}|ys={}|x2s={mantissa:1.5:decimal}|xds={}|yds={}|entry=open{1.5}|vp=none|se=none|svp=none|vem=_|vemt=0|vsm=_|ves=_|vss=_|vest=0");
  expect("stateExp", "3", "2", "", "x={decimal:300:normalized}|y={}|x2={}|xs={exponent:3:2:decimal,mantissa:300:decimal}|ys={}|x2s={exponent:3:2:decimal,mantissa:300:decimal}|xds={}|yds={}|entry=exp{3};{2}|vp=exp{3};{2}|se=none|svp=none|vem=_|vemt=0|vsm=_|ves=_|vss={mantissa:300:decimal}|vest=0");
  expect("stateExp", "2.5", "-1", "", "x={decimal:0.25:normalized}|y={}|x2={}|xs={exponent:2.5:-1:decimal}|ys={}|x2s={exponent:2.5:-1:decimal}|xds={}|yds={}|entry=exp{2.5};{-1}|vp=exp{2.5};{-1}|se=none|svp=none|vem=_|vemt=0|vsm=_|ves=_|vss={exponent:2.5:-1:decimal}|vest=0");
  expect("stateStructExp", "hex:5:mantissa", "2", "", "x={}|y={}|x2={}|xs={hex-exponent:5:2}|ys={}|x2s={hex-exponent:5:2}|xds={}|yds={}|entry=closed|vp=none|se=exp{hex:5:mantissa};{2}|svp=exp{hex:5:mantissa};{2}|vem=_|vemt=0|vsm=_|ves=_|vss={hex:500:mantissa}|vest=0");
  expect("stateMantissaShapes", "3,5", "", "", "x={decimal:3:normalized,decimal:5:normalized}|y={}|x2={decimal:3:normalized,decimal:5:normalized}|xs={mantissa:3:decimal,mantissa:5:decimal}|ys={}|x2s={mantissa:3:decimal,mantissa:5:decimal}|xds={}|yds={}|entry=closed|vp=none|se=none|svp=none|vem={3,5}|vemt=0|vsm={3,5}|ves=_|vss={mantissa:3:decimal,mantissa:5:decimal}|vest=0");
  expect("stateMantissaShapes", "bad", "", "", "undefined");
  expect("stateStructShapes", "hex:5:mantissa,hex:A:mantissa", "", "", "x={}|y={}|x2={}|xs={hex:5:mantissa,hex:A:mantissa}|ys={}|x2s={hex:5:mantissa,hex:A:mantissa}|xds={hex:5:mantissa,hex:A:mantissa}|yds={}|entry=closed|vp=none|se=none|svp=none|vem=_|vemt=0|vsm=_|ves={hex:5:mantissa,hex:A:mantissa}|vss={hex:5:mantissa,hex:A:mantissa}|vest=0");
  expect("signedDecimalVp", "3", "2", "", "x={decimal:300:normalized}|y={}|x2={decimal:300:normalized}|xs={exponent:3:2:decimal,mantissa:300:decimal}|ys={}|x2s={exponent:3:2:decimal,mantissa:300:decimal}|xds={}|yds={}|entry=closed|vp=exp{3};{2}|se=none|svp=none|vem={300}|vemt=0|vsm=_|ves=_|vss={mantissa:300:decimal}|vest=0");
  expect("signedDecimalVp", "bad", "2", "", "x={}|y={}|x2={}|xs={}|ys={}|x2s={}|xds={}|yds={}|entry=closed|vp=unknown|se=none|svp=none|vem=_|vemt=0|vsm=_|ves=_|vss=_|vest=0");
  expect("signedStructVp", "hex:5:mantissa", "2", "", "x={}|y={}|x2={}|xs={hex-exponent:5:2}|ys={}|x2s={hex-exponent:5:2}|xds={}|yds={}|entry=closed|vp=none|se=none|svp=exp{hex:5:mantissa};{2}|vem=_|vemt=0|vsm=_|ves={hex:500:mantissa}|vss={hex:500:mantissa}|vest=0");
  expect("signedStructVp", "bad", "2", "", "x={}|y={}|x2={}|xs={}|ys={}|x2s={}|xds={}|yds={}|entry=closed|vp=none|se=none|svp=unknown|vem=_|vemt=0|vsm=_|ves=_|vss=_|vest=0");
  expect("closeExp", "3", "2", "", "x={decimal:300:normalized}|y={}|x2={decimal:300:normalized}|xs={exponent:3:2:decimal,mantissa:300:decimal}|ys={}|x2s={exponent:3:2:decimal,mantissa:300:decimal}|xds={exponent:3:2:decimal,mantissa:300:decimal}|yds={}|entry=closed|vp=none|se=none|svp=none|vem={300}|vemt=0|vsm=_|ves=_|vss={mantissa:300:decimal}|vest=0");
  expect("closeExp", "bad", "2", "", "x={}|y={}|x2={}|xs={}|ys={}|x2s={}|xds={}|yds={}|entry=closed|vp=none|se=none|svp=none|vem=_|vemt=0|vsm=_|ves={}|vss={}|vest=0");
  expect("closeStruct", "hex:5:mantissa", "2", "", "x={}|y={}|x2={}|xs={hex-exponent:5:2}|ys={}|x2s={hex-exponent:5:2}|xds={hex-exponent:5:2}|yds={}|entry=closed|vp=none|se=none|svp=exp{hex:5:mantissa};{2}|vem=_|vemt=0|vsm=_|ves={hex:500:mantissa}|vss={hex:500:mantissa}|vest=0");
  expect("closeStruct", "bad", "2", "", "x={}|y={}|x2={}|xs={}|ys={}|x2s={}|xds={}|yds={}|entry=closed|vp=none|se=none|svp=unknown|vem=_|vemt=0|vsm=_|ves=_|vss=_|vest=0");
}

}  // namespace mkpro::tests
