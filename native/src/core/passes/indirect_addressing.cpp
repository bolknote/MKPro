#include "mkpro/core/passes/indirect_addressing.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_indirect_flow.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>

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
  bool number_entry_active = false;
  std::map<std::string, std::string> stable_registers;
  std::map<std::string, std::optional<std::string>> logical_registers;
};

void clear_known_state(KnownState& state) {
  state.current_literal.reset();
  state.number_entry_active = false;
  state.stable_registers.clear();
  state.logical_registers.clear();
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

void remember_store(KnownState& state, const IrOp& op) {
  const std::string& register_name = op.register_name;
  if (!mkpro::core::is_stable_indirect_selector(register_name))
    return;
  std::string value;
  if (!literal_number(state.current_literal, value)) {
    state.stable_registers.erase(register_name);
    state.logical_registers.erase(register_name);
  } else {
    state.stable_registers[register_name] = value;
    state.logical_registers[register_name] = op.meta.logical_register_name;
  }
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
    state.current_literal = (state.number_entry_active
                                 ? state.current_literal.value_or("") : "") + *digit;
    state.number_entry_active = true;
    return;
  }

  state.number_entry_active = false;
  switch (op.kind) {
  case IrKind::Store:
    remember_store(state, op);
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

} // namespace

PassResult stable_indirect_flow(const std::vector<IrOp>& ops, const PassContext& context) {
  // Symbolic branches are handled by the ordinary layout passes. A numeric
  // operand needs the exact command-identity and selector transport proof.
  const bool has_numeric_flow = std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return !has_rewrite_barrier(op) && std::holds_alternative<int>(op.target) &&
           (op.kind == IrKind::Jump || op.kind == IrKind::Call ||
            op.kind == IrKind::CondJump);
  });
  if (!has_numeric_flow)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  const auto rewritten = mkpro::core::optimize_post_layout_charged_selector_flow(
      lower_ir_to_machine(ops), {}, context.options);
  if (rewritten.applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  return PassResult{
      .ops = raise_machine_to_ir(rewritten.items,
                                 effective_optimizer_feature_profile(context.options)),
      .applied = rewritten.applied,
      .optimizations = {{
          .name = "stable-indirect-flow",
          .detail = "Replaced " + std::to_string(rewritten.applied) +
                    " direct flow(s) using exact runtime selector and address transport proofs.",
      }},
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
  std::vector<IrOp> result;
  result.reserve(ops.size());
  KnownState state;
  int applied = 0;
  const auto model =
      address_space_model_for_feature_profile(effective_optimizer_feature_profile(context.options));
  std::optional<mkpro::core::StableRegisterValueFlow> values;
  std::vector<std::size_t> item_indexes;
  const auto selector_is_proved = [&](std::size_t index, const std::string& selector,
                                     int target) {
    if (!values.has_value()) {
      const auto items = lower_ir_to_machine(ops);
      std::size_t offset = 0;
      for (const auto& op : ops) {
        item_indexes.push_back(offset);
        offset += lower_ir_to_machine({op}).size();
      }
      mkpro::core::PostLayoutControlFlowOptions options;
      options.address_space_model = model;
      options.empty_return_target = 1;
      const auto flow = mkpro::core::build_post_layout_control_flow(items, options);
      values = mkpro::core::analyze_stable_register_value_flow(items, {}, flow, model);
    }
    if (!values->proved)
      return false;
    const auto before = values->before_item.find(item_indexes.at(index));
    if (before == values->before_item.end())
      return false;
    const auto& literal = before->second.at(
        static_cast<std::size_t>(register_index(selector) - 7));
    if (!literal.has_value())
      return false;
    const auto evaluated = mkpro::core::evaluate_indirect_address(
        selector, *literal, mkpro::core::IndirectOperationKind::Memory, model);
    return evaluated.has_value() && evaluated->memory_target == target &&
           mkpro::core::indirect_writeback_preserves_literal_value(*evaluated, *literal);
  };

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if ((op.kind == IrKind::Recall || op.kind == IrKind::Store) && !has_rewrite_barrier(op) &&
        !is_display_focus_sensitive(op)) {
      // The cheap linear state only locates candidates. In particular a label
      // in the middle of a literal cannot certify a new number-entry buffer.
      const auto selector = find_memory_selector(state, op.register_name);
      if (selector.has_value()) {
        const auto logical_selector = state.logical_registers.at(*selector);
        const bool complete_identity =
            logical_selector.has_value() == op.meta.logical_register_name.has_value();
        if (complete_identity &&
            selector_is_proved(index, *selector, register_index(op.register_name))) {
          IrOp rewritten = op;
          rewritten.kind = op.kind == IrKind::Recall ? IrKind::IndirectRecall : IrKind::IndirectStore;
          rewritten.register_name = *selector;
          rewritten.opcode = (op.kind == IrKind::Recall ? 0xd0 : 0xb0) + register_index(*selector);
          rewritten.meta.mnemonic =
              (op.kind == IrKind::Recall ? "К П->X " : "К X->П ") + *selector;
          rewritten.meta = clone_meta(
              rewritten.meta, "indirect memory table indirect-memory-target=" + op.register_name);
          rewritten.meta.indirect_memory_targets = std::vector<int>{register_index(op.register_name)};
          rewritten.meta.logical_register_name = logical_selector;
          rewritten.meta.logical_indirect_memory_targets.reset();
          if (op.meta.logical_register_name.has_value())
            rewritten.meta.logical_indirect_memory_targets =
                std::vector<std::string>{*op.meta.logical_register_name};
          result.push_back(rewritten);
          update_known_after_op(state, rewritten);
          ++applied;
          continue;
        }
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
      .optimizations = {{
          .name = "indirect-memory-table",
          .detail = "Rewrote " + std::to_string(applied) +
                    " memory access(es) using proved stable values and separate logical identities.",
      }},
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
