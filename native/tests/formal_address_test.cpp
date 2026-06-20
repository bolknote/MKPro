#include "mkpro/core/formal_address.hpp"

#include "test_support.hpp"

#include <stdexcept>
#include <string>

namespace mkpro::tests {

namespace {

void require_throws_outside_address() {
  try {
    (void)official_address_to_opcode(105);
  } catch (const std::exception& error) {
    require(std::string(error.what()).find("outside 00..A4") != std::string::npos,
            "official address error text should mention outside 00..A4");
    return;
  }
  throw std::runtime_error("official_address_to_opcode(105) should throw");
}

}  // namespace

void formal_address_matches_typescript_contract() {
  require(official_address_to_opcode(0) == 0x00, "address 0 should encode as 00");
  require(official_address_to_opcode(99) == 0x99, "address 99 should encode as 99");
  require(official_address_to_opcode(100) == 0xa0, "address 100 should encode as A0");
  require(official_address_to_opcode(104) == 0xa4, "address 104 should encode as A4");
  require_throws_outside_address();

  const auto a5 = formal_address_info(0xa5);
  require(a5.ordinal == 105 && a5.actual == 0 && a5.kind == FormalAddressKind::ShortSide,
          "A5 should map to short-side actual 0");
  const auto b1 = formal_address_info(0xb1);
  require(b1.ordinal == 111 && b1.actual == 6 && b1.kind == FormalAddressKind::ShortSide,
          "B1 should map to short-side actual 6");
  const auto b2 = formal_address_info(0xb2);
  require(b2.ordinal == 112 && b2.actual == 0 && b2.kind == FormalAddressKind::LongSide,
          "B2 should map to long-side actual 0");
  const auto c5 = formal_address_info(0xc5);
  require(c5.ordinal == 125 && c5.actual == 13 && c5.kind == FormalAddressKind::Dark,
          "C5 should map to dark actual 13");
  const auto f9 = formal_address_info(0xf9);
  require(f9.ordinal == 159 && f9.actual == 47 && f9.kind == FormalAddressKind::Dark,
          "F9 should map to dark actual 47");

  const auto fa = formal_address_info(0xfa);
  require(fa.ordinal == 160 && fa.actual == 48 && fa.kind == FormalAddressKind::SuperDark &&
              fa.one_command && fa.extra == 1,
          "FA should map to one-command super-dark continuation 1");
  const auto ff = formal_address_info(0xff);
  require(ff.ordinal == 165 && ff.actual == 53 && ff.kind == FormalAddressKind::SuperDark &&
              ff.one_command && ff.extra == 6,
          "FF should map to one-command super-dark continuation 6");

  require(formal_address_ordinal(0x9f) == 105, "9F ordinal should be 105");
  require(formal_address_info(0x9f).actual == 0, "9F actual should be 0");
  require(formal_address_info(0xac).ordinal == 112, "AC ordinal should be 112");
  require(formal_address_info(0xac).actual == 0, "AC actual should be 0");

  require(parse_formal_address_opcode("C5") == 0xc5, "C5 should parse");
  require(parse_formal_address_opcode(".5") == 0xa5, ".5 should parse as A5");
  require(format_formal_address_opcode(0xfb) == "FB", "FB should format as uppercase hex");
}

}  // namespace mkpro::tests
