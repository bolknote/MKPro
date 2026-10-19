#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core::detail::exact_terms {
inline bool modeled_plain(int code) {
  return (code >= 0 && code <= 9) || code == 0x0a || code == 0x14 ||
      code == 0x25 || (code >= 0x10 && code <= 0x13) || code == 0x15 ||
      code == 0x22 || code == 0x31 || code == 0x32 ||
      (code >= 0x34 && code <= 0x39);
}

// Exact terms, not floating-point samples. Unknown arithmetic is kept in its
// original order. Its entire incoming machine state is also an observation:
// an error must not expose a different register or stack before convergence.
struct Value {
  std::string key;
  std::optional<std::string> decimal;
  bool operator==(const Value&) const = default;
};
inline Value symbolic(const std::string& name) { return {"$" + name, std::nullopt}; }
inline std::optional<Value> number(const std::string& value) {
  const auto exact = passes::concrete_decimal_binary_value(0x10, value, "0");
  if (!exact) return std::nullopt;
  return Value{"#" + *exact, *exact};
}
struct State {
  std::array<Value, 4> stack;
  std::array<Value, 15> registers;
  Value x1, x2;
  std::string literal;
  bool operator==(const State&) const = default;
};
inline State initial() {
  State s;
  for (std::size_t i = 0; i < s.stack.size(); ++i)
    s.stack[i] = symbolic("stack" + std::to_string(i));
  for (std::size_t i = 0; i < s.registers.size(); ++i)
    s.registers[i] = symbolic("register" + std::to_string(i));
  s.x1 = symbolic("last-X");
  s.x2 = symbolic("entry-shadow");
  return s;
}
struct Observation {
  int opcode = 0;
  State before;
  bool operator==(const Observation&) const = default;
};
struct Evaluation {
  State state = initial();
  std::vector<Observation> observations;
  bool valid = true;

  void synchronize_call() {
    state.x2 = state.stack[0];
    state.literal.clear();
  }

  void execute(const IrOp& op) {
    if (!valid) return;
    const auto old = state.stack;
    const int code = op.opcode;
    if (op.kind == IrKind::Recall || op.kind == IrKind::Store) {
      if (op.register_name.size() != 1U ||
          op.register_name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        valid = false;
        return;
      }
      const int reg = register_index(op.register_name);
      // This exact-term domain models the stock bank. A larger target bank
      // or malformed IR is unsupported, not a compiler exception.
      if (reg < 0 || reg >= static_cast<int>(state.registers.size())) {
        valid = false;
        return;
      }
    }
    if (op.kind == IrKind::Recall) {
      state.stack = {state.registers.at(static_cast<std::size_t>(register_index(op.register_name))),
                     old[0], old[1], old[2]};
      state.x2 = old[0];
      state.literal.clear();
      return;
    }
    if (op.kind == IrKind::Store) {
      state.registers.at(static_cast<std::size_t>(register_index(op.register_name))) = old[0];
      state.literal.clear();
      return;
    }
    if (op.kind != IrKind::Plain || !modeled_plain(code)) { valid = false; return; }
    if (code >= 0 && code <= 9) {
      if (state.literal.empty()) state.stack = {old[0], old[0], old[1], old[2]};
      state.literal.push_back(static_cast<char>('0' + code));
      const auto value = number(state.literal);
      if (!value || state.literal.size() > 8U) { valid = false; return; }
      state.stack[0] = *value;
      state.x2 = *value;
      return;
    }
    if (code == 0x0a) {
      if (state.literal.empty() || state.literal.find('.') != std::string::npos) {
        valid = false;
        return;
      }
      state.literal += '.';
      return;
    }
    if (code == 0x14) {
      state.stack = {old[1], old[0], old[2], old[3]};
    } else if (code == 0x25) {
      state.stack = {old[1], old[2], old[3], old[0]};
    } else {
      const bool binary = (code >= 0x10 && code <= 0x13) ||
                          (code >= 0x36 && code <= 0x39);
      Value value;
      if (old[0].decimal && (!binary || old[1].decimal)) {
        const auto exact = binary
            ? passes::concrete_decimal_binary_value(code, *old[1].decimal, *old[0].decimal)
            : passes::concrete_decimal_unary_value(code, *old[0].decimal);
        const auto evaluated = exact ? number(*exact) : std::nullopt;
        if (!evaluated) { valid = false; return; }
        value = *evaluated;
      } else {
        observations.push_back({code, state});
        value = {"(" + std::to_string(code) + ":" +
                 (binary ? old[1].key + "," : std::string{}) + old[0].key + ")", std::nullopt};
      }
      const auto& info = opcode_by_code(code);
      if (binary) {
        if (info.stack_effect == StackEffect::ConsumeYDrop)
          state.stack = {value, old[2], old[3], old[3]};
        else if (info.stack_effect == StackEffect::ConsumeYKeep)
          state.stack = {value, old[1], old[2], old[3]};
        else { valid = false; return; }
      } else {
        if (info.stack_effect != StackEffect::Preserves) { valid = false; return; }
        state.stack[0] = value;
      }
      if (info.x2_effect == X2Effect::Affects) state.x2 = old[0];
      else if (info.x2_effect != X2Effect::Preserves) { valid = false; return; }
    }
    if (opcode_saves_physical_x1(code)) state.x1 = old[0];
    state.literal.clear();
  }

  void call(const std::vector<IrOp>& body) {
    synchronize_call();
    for (const auto& op : body) execute(op);
    // This proof does not model a return while mantissa entry is still open.
    if (!state.literal.empty()) valid = false;
    synchronize_call();
  }
};


} // namespace mkpro::core::detail::exact_terms
