#include "mkpro/core/passes/indirect_addressing.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <cctype>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct KnownState {
  std::optional<std::string> current_literal;
  std::map<std::string, std::string> stable_registers;
};

void clear_known_state(KnownState& state) {
  state.current_literal.reset();
  state.stable_registers.clear();
}

IrMeta clone_meta(IrMeta meta, const std::string& comment) {
  if (meta.comment.has_value() && !meta.comment->empty()) {
    meta.comment = *meta.comment + "; " + comment;
  } else {
    meta.comment = comment;
  }
  return meta;
}

std::optional<std::string> digit_for_plain(const IrOp& op) {
  if (op.kind != IrKind::Plain || op.opcode < 0x00 || op.opcode > 0x09)
    return std::nullopt;
  return std::to_string(op.opcode);
}

bool literal_number(const std::optional<std::string>& text, std::string& output) {
  if (!text.has_value() || text->empty())
    return false;
  for (const char ch : *text) {
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
      return false;
  }
  output = *text;
  return true;
}

void remember_store(KnownState& state, const std::string& register_name) {
  if (!mkpro::core::is_stable_indirect_selector(register_name))
    return;
  std::string value;
  if (!literal_number(state.current_literal, value)) {
    state.stable_registers.erase(register_name);
  } else {
    state.stable_registers[register_name] = value;
  }
}

std::optional<std::string> find_flow_selector(const KnownState& state, const IrTarget& target) {
  const auto* numeric_target = std::get_if<int>(&target);
  if (numeric_target == nullptr)
    return std::nullopt;
  for (const auto& [register_name, value] : state.stable_registers) {
    const std::optional<mkpro::core::IndirectAddressEvaluation> evaluated =
        mkpro::core::evaluate_indirect_address(register_name, value,
                                               mkpro::core::IndirectOperationKind::Flow);
    if (evaluated.has_value() && evaluated->flow_target == *numeric_target)
      return register_name;
  }
  return std::nullopt;
}

std::optional<std::string> find_memory_selector(const KnownState& state,
                                                const std::string& target_register) {
  const int target = register_index(target_register);
  for (const auto& [register_name, value] : state.stable_registers) {
    const std::optional<mkpro::core::IndirectAddressEvaluation> evaluated =
        mkpro::core::evaluate_indirect_address(register_name, value,
                                               mkpro::core::IndirectOperationKind::Memory);
    if (evaluated.has_value() && evaluated->memory_target == target)
      return register_name;
  }
  return std::nullopt;
}

void update_known_after_op(KnownState& state, const IrOp& op) {
  if (has_rewrite_barrier(op)) {
    clear_known_state(state);
    return;
  }

  if (const std::optional<std::string> digit = digit_for_plain(op)) {
    state.current_literal = state.current_literal.value_or("") + *digit;
    return;
  }

  switch (op.kind) {
  case IrKind::Store:
    remember_store(state, op.register_name);
    return;
  case IrKind::Recall: {
    const auto found = state.stable_registers.find(op.register_name);
    if (found == state.stable_registers.end()) {
      state.current_literal.reset();
    } else {
      state.current_literal = found->second;
    }
    return;
  }
  case IrKind::Label:
  case IrKind::IndirectStore:
  case IrKind::Call:
  case IrKind::IndirectCall:
  case IrKind::CondJump:
  case IrKind::IndirectCondJump:
  case IrKind::Stop:
  case IrKind::Jump:
  case IrKind::IndirectJump:
  case IrKind::Return:
    clear_known_state(state);
    return;
  default:
    state.current_literal.reset();
    return;
  }
}

int indirect_cond_base(const std::string& condition) {
  if (condition == "!=0")
    return 0x70;
  if (condition == ">=0")
    return 0x90;
  if (condition == "<0")
    return 0xc0;
  return 0xe0;
}

std::string indirect_cond_name(const std::string& condition) {
  if (condition == "==0")
    return "x=0";
  if (condition == "!=0")
    return "x!=0";
  return "x" + condition;
}

IrOp indirect_flow_op(const IrOp& op, const std::string& register_name, int target) {
  const int offset = register_index(register_name);
  const std::string suffix = "stable indirect flow indirect-target=" + std::to_string(target);
  IrOp result = op;
  result.register_name = register_name;
  result.target = 0;
  result.target_meta = {};
  if (op.kind == IrKind::Jump) {
    result.kind = IrKind::IndirectJump;
    result.opcode = 0x80 + offset;
    result.meta.mnemonic = "К БП " + register_name;
  } else if (op.kind == IrKind::Call) {
    result.kind = IrKind::IndirectCall;
    result.opcode = 0xa0 + offset;
    result.meta.mnemonic = "К ПП " + register_name;
  } else {
    result.kind = IrKind::IndirectCondJump;
    result.opcode = indirect_cond_base(op.condition) + offset;
    result.meta.mnemonic = "К " + indirect_cond_name(op.condition) + " " + register_name;
  }
  result.meta = clone_meta(result.meta, suffix);
  return result;
}

} // namespace

PassResult stable_indirect_flow(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  std::vector<IrOp> result;
  result.reserve(ops.size());
  KnownState state;
  int applied = 0;

  for (const IrOp& op : ops) {
    if (!has_rewrite_barrier(op) &&
        (op.kind == IrKind::Jump || op.kind == IrKind::Call || op.kind == IrKind::CondJump)) {
      const std::optional<std::string> selector = find_flow_selector(state, op.target);
      if (selector.has_value()) {
        const int target = std::get<int>(op.target);
        IrOp rewritten = indirect_flow_op(op, *selector, target);
        result.push_back(rewritten);
        update_known_after_op(state, rewritten);
        ++applied;
        continue;
      }
    }
    result.push_back(op);
    update_known_after_op(state, op);
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "stable-indirect-flow",
                  .detail = "Replaced " + std::to_string(applied) +
                            " direct branch/call(s) with stable-register indirect flow.",
              },
          },
  };
}

IrPass stable_indirect_flow_pass() {
  return IrPass{
      .name = "stable-indirect-flow",
      .run = stable_indirect_flow,
      .layout_safe = false,
  };
}

PassResult indirect_memory_table(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  std::vector<IrOp> result;
  result.reserve(ops.size());
  KnownState state;
  int applied = 0;

  for (const IrOp& op : ops) {
    if ((op.kind == IrKind::Recall || op.kind == IrKind::Store) && !has_rewrite_barrier(op) &&
        !is_display_focus_sensitive(op)) {
      const std::optional<std::string> selector = find_memory_selector(state, op.register_name);
      if (selector.has_value()) {
        IrOp rewritten = op;
        rewritten.kind = op.kind == IrKind::Recall ? IrKind::IndirectRecall : IrKind::IndirectStore;
        rewritten.register_name = *selector;
        rewritten.opcode = (op.kind == IrKind::Recall ? 0xd0 : 0xb0) + register_index(*selector);
        rewritten.meta.mnemonic =
            (op.kind == IrKind::Recall ? "К П->X " : "К X->П ") + *selector;
        rewritten.meta = clone_meta(
            rewritten.meta,
            "indirect memory table indirect-memory-target=" + op.register_name);
        result.push_back(rewritten);
        update_known_after_op(state, rewritten);
        ++applied;
        continue;
      }
    }
    result.push_back(op);
    update_known_after_op(state, op);
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "indirect-memory-table",
                  .detail = "Rewrote " + std::to_string(applied) +
                            " direct memory access(es) through an existing stable selector.",
              },
          },
  };
}

IrPass indirect_memory_table_pass() {
  return IrPass{
      .name = "indirect-memory-table",
      .run = indirect_memory_table,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
