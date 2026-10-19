#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/opcodes.hpp"

#include <array>
#include <optional>

namespace mkpro::core::detail {

// Same default-profile single-command classification as the IR raiser.
// Lazy per-thread slots preserve exceptions and never parse unused opcodes.
inline IrKind basic_opcode_ir_kind(int opcode) {
  const auto raise = [&] {
    const auto ir = raise_machine_to_ir(
        {MachineItem::op(opcode, opcode_by_code(opcode).name)});
    return ir.empty() ? IrKind::Plain : ir.front().kind;
  };
  if (opcode < 0 || opcode > 255) return raise();
  static thread_local std::array<std::optional<IrKind>, 256> kinds;
  auto& kind = kinds[static_cast<std::size_t>(opcode)];
  if (!kind) kind = raise();
  return *kind;
}

} // namespace mkpro::core::detail

