#include "mkpro/core/passes/indirect_selector_integer_part.hpp"

#include "mkpro/core/indirect_addressing.hpp"

#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kIntOpcode = 0x34;

std::optional<std::string> marked_integer_part(const IrOp& op) {
  if (op.kind != IrKind::IndirectRecall && op.kind != IrKind::IndirectStore)
    return std::nullopt;
  if (!mkpro::core::is_stable_indirect_selector(op.register_name))
    return std::nullopt;
  if (!op.meta.comment.has_value())
    return std::nullopt;

  static const std::regex marker(R"(\bindirect-selector-integer-part=([A-Za-z_]\w*)\b)");
  std::smatch match;
  if (!std::regex_search(*op.meta.comment, match, marker))
    return std::nullopt;
  if (match.size() < 2U)
    return std::nullopt;
  return match[1].str();
}

bool mutates_selector_register(const IrOp& op, const std::string& register_name) {
  if (op.kind == IrKind::Store && op.register_name == register_name)
    return true;
  if (op.kind == IrKind::IndirectRecall && op.register_name == register_name)
    return true;
  if (op.kind == IrKind::IndirectStore && op.register_name == register_name)
    return true;
  if (op.kind == IrKind::IndirectJump && op.register_name == register_name)
    return true;
  if (op.kind == IrKind::IndirectCall && op.register_name == register_name)
    return true;
  if (op.kind == IrKind::IndirectCondJump && op.register_name == register_name)
    return true;
  return false;
}

} // namespace

PassResult indirect_selector_integer_part(const std::vector<IrOp>& ops,
                                          const PassContext& context) {
  (void)context;

  std::map<std::string, std::string> integer_part_registers;
  std::set<std::size_t> remove;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (has_rewrite_barrier(op)) {
      integer_part_registers.clear();
      continue;
    }

    if (op.kind == IrKind::Recall) {
      const auto source = integer_part_registers.find(op.register_name);
      const IrOp* next = index + 1U < ops.size() ? &ops.at(index + 1U) : nullptr;
      if (source != integer_part_registers.end() && next != nullptr &&
          next->kind == IrKind::Plain && next->opcode == kIntOpcode &&
          !has_rewrite_barrier(*next)) {
        remove.insert(index + 1U);
      }
      continue;
    }

    const std::optional<std::string> marked = marked_integer_part(op);
    if (marked.has_value() &&
        (op.kind == IrKind::IndirectRecall || op.kind == IrKind::IndirectStore)) {
      integer_part_registers[op.register_name] = *marked;
      continue;
    }

    std::vector<std::string> to_delete;
    for (const auto& [register_name, unused_source] : integer_part_registers) {
      (void)unused_source;
      if (mutates_selector_register(op, register_name))
        to_delete.push_back(register_name);
    }
    for (const std::string& register_name : to_delete) {
      integer_part_registers.erase(register_name);
    }
  }

  if (remove.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size() - remove.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!remove.contains(index))
      result.push_back(ops.at(index));
  }

  const int applied = static_cast<int>(remove.size());
  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          std::vector<AppliedOptimization>{
              AppliedOptimization{
                  .name = "indirect-selector-integer-part-reuse",
                  .detail = "Removed " + std::to_string(applied) + " redundant К [x] cell" +
                            (applied == 1 ? "" : "s") +
                            " after a proved fractional indirect selector had already truncated "
                            "its register to the integer part.",
              },
          },
  };
}

IrPass indirect_selector_integer_part_pass() {
  return IrPass{
      .name = "indirect-selector-integer-part",
      .run = indirect_selector_integer_part,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
