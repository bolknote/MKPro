#include "mkpro/core/detail/decimal_mantissa_syntax.hpp"
#include "mkpro/core/detail/basic_opcode_ir_kind.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "test_support.hpp"

#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using tests::require;

void opcode_kinds() {
  for (int pass = 0; pass < 2; ++pass)
    for (int opcode = 0; opcode <= 255; ++opcode) {
      const auto original = raise_machine_to_ir(
          {MachineItem::op(opcode, opcode_by_code(opcode).name)});
      const auto expected = original.empty() ? IrKind::Plain : original.front().kind;
      require(detail::basic_opcode_ir_kind(opcode) == expected,
              "cold and warm opcode classification matches the original IR raiser");
    }
}

void decimal_syntax() {
  const std::regex original(R"(^(-?)([0-9]{1,8})(?:\.([0-9]*))?$)");
  const auto check = [&](const std::string& text) {
    std::smatch matched;
    const bool syntax = std::regex_match(text, matched, original);
    const auto digits = syntax ? matched[2].length() + matched[3].length() : 0;
    require(detail::is_plain_decimal_mantissa(text) == (syntax && digits <= 8),
            "ASCII entry scanner must exactly match the previous complete regex and digit bound");
  };
  std::vector<std::string> frontier{std::string{}};
  for (int length = 0; length <= 5; ++length) {
    std::vector<std::string> next;
    for (const auto& text : frontier) {
      check(text);
      if (length != 5)
        for (const char symbol : std::string_view{"-01. e+"})
          next.push_back(text + symbol);
    }
    frontier = std::move(next);
  }
  for (std::size_t integer = 0; integer <= 10; ++integer)
    for (std::size_t fraction = 0; fraction <= 10; ++fraction)
      for (const std::string& sign : {std::string{}, std::string{"-"}}) {
        check(sign + std::string(integer, '0') + "." + std::string(fraction, '9'));
        check(sign + std::string(integer, '9'));
      }
  for (const std::string text : {"-0", "-0.", "00000000", "12345678.",
                                 "1.2345678", "1.23456789", "1e2", "1E-2",
                                 "0\n", "1,2", "8.ABCDEF0", "--1"})
    check(text);
  check(std::string{"1\0", 2});
  check("\xD9\xA1");
  check("\xE2\x88\x92" "1");
}

IrOp command(int opcode) {
  std::vector<MachineItem> image{MachineItem::op(opcode, opcode_by_code(opcode).name)};
  if (opcode_by_code(opcode).takes_address)
    image.push_back(MachineItem::address(0));
  return raise_machine_to_ir(image).front();
}

void borrowed_transfer() {
  using namespace passes;
  const std::vector<std::vector<int>> prefixes{
      {}, {1, 2}, {1, 0x0a, 2}, {1, 0x0c, 2, 0x0b},
      {8, 0x0a, 1, 0x0e, 3, 0x38, 0x48, 0x68},
      {5, 0x0e, 8, 0x38, 0x3a}, {0, 0x0b, 0x0c},
      {0x69, 0x0e, 0x34, 0x0f}};
  {
    auto zero = empty_x2_value_dataflow_state();
    for (const int code : {0, 0x0a})
      zero = *transfer_x2_value_state_for_edge(
          zero, command(code), X2DataflowEdgeKind::Normal);
    require(zero.x.contains("decimal:0:normalized"),
            "a trailing entry point retains the normalized zero fact");
  }
  const std::vector<int> codes{0, 7, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x21, 0x22, 0x25, 0x30,
      0x31, 0x32, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
      0x48, 0x52, 0x57, 0x59, 0x5c, 0x5e, 0x68, 0x78, 0xa8, 0xb0, 0xd0};
  for (const auto& prefix : prefixes) {
    auto state = empty_x2_value_dataflow_state(true);
    int position = 0;
    for (const int code : prefix) {
      const auto next = transfer_x2_value_state_for_edge(
          std::optional{state}, command(code), X2DataflowEdgeKind::Normal,
          {.track_register_memory = true}, position++);
      require(next.has_value(), "test entry prefix has a complete abstract state");
      state = *next;
    }
    const auto before = state;
    for (const int code : codes)
      for (const auto edge : {X2DataflowEdgeKind::Normal,
                              X2DataflowEdgeKind::Fallthrough, X2DataflowEdgeKind::Jump})
        for (const bool memory : {false, true})
          for (const bool vp : {false, true}) {
            const X2TransferStateOptions options{
                .track_register_memory = memory, .target_starts_with_vp = vp};
            const auto op = command(code);
            const auto owned = transfer_x2_value_state_for_edge(std::optional{state}, op, edge, options, 17);
            const auto borrowed = transfer_x2_value_state_for_edge(state, op, edge, options, 17);
            require(owned == borrowed && state == before,
                    "borrowed input preserves every optional, shape, memory, entry and VP field without mutation");
            if (owned) {
              require(same_x2_value_dataflow_state(state, *owned) ==
                          same_x2_value_dataflow_state(std::optional{state}, std::optional{*owned}) &&
                          same_x2_value_dataflow_state(std::optional{state}, *owned) ==
                              same_x2_value_dataflow_state(state, std::optional{*owned}),
                      "all borrowed comparison overloads retain the legacy semantic equality");
              require(!same_x2_value_dataflow_state(std::nullopt, *owned) &&
                          !same_x2_value_dataflow_state(*owned, std::nullopt),
                      "absent comparisons remain distinct from a present empty state");
              require(join_x2_value_dataflow_states(&state, *owned, memory) ==
                          join_x2_value_dataflow_states(std::optional{state}, *owned, memory) &&
                          join_x2_value_dataflow_states(nullptr, *owned, memory) ==
                              join_x2_value_dataflow_states(std::nullopt, *owned, memory),
                      "borrowed and optional joins retain every field and bottom-state memory projection");
            }
            require(!transfer_x2_value_state_for_edge(std::nullopt, op, edge, options, 17),
                    "absent input never creates an abstract state");
          }
  }
}
} // namespace

int main() {
  try {
    opcode_kinds();
    decimal_syntax();
    borrowed_transfer();
    std::cout << "X2 hot path: exact syntax and borrowed transfer passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
