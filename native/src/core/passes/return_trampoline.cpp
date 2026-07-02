#include "mkpro/core/passes/return_trampoline.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr std::array<std::string_view, 8> kStableRegisters = {
    "7", "8", "9", "a", "b", "c", "d", "e",
};
constexpr std::string_view kReturnZeroSelector = "B2";

struct SelectorPlan {
  std::string register_name;
  std::string selector_value;
  bool existing_preload = false;
};

IrMeta clone_meta(IrMeta meta, const std::string& comment) {
  if (meta.comment.has_value() && !meta.comment->empty()) {
    meta.comment = *meta.comment + "; " + comment;
  } else {
    meta.comment = comment;
  }
  return meta;
}

bool references_register(const IrOp& op, const std::string& register_name) {
  if (op.kind == IrKind::Store || op.kind == IrKind::Recall ||
      op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall ||
      op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
      op.kind == IrKind::IndirectCondJump) {
    return op.register_name == register_name;
  }
  return false;
}

bool register_is_used(const std::vector<IrOp>& ops, const std::string& register_name) {
  return std::any_of(ops.begin(), ops.end(),
                     [&](const IrOp& op) { return references_register(op, register_name); });
}

bool register_is_overwritten(const std::vector<IrOp>& ops, const std::string& register_name) {
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Store && op.register_name == register_name)
      return true;
    if (op.kind != IrKind::IndirectStore)
      continue;
    const std::optional<std::set<std::string>> targets = known_indirect_memory_targets(op);
    if (!targets.has_value() || targets->contains(register_name))
      return true;
  }
  return false;
}

bool selector_targets_return_zero(const std::string& register_name, const std::string& value) {
  const std::optional<mkpro::core::IndirectAddressEvaluation> evaluated =
      mkpro::core::evaluate_indirect_address(register_name, value,
                                             mkpro::core::IndirectOperationKind::Flow);
  return evaluated.has_value() && evaluated->actual_flow_target == 0;
}

std::optional<SelectorPlan>
existing_return_zero_selector(const std::vector<IrOp>& ops,
                              const std::map<std::string, std::string>& preloaded) {
  for (const std::string_view register_view : kStableRegisters) {
    const std::string register_name(register_view);
    const auto existing = preloaded.find(register_name);
    if (existing == preloaded.end() || register_is_overwritten(ops, register_name))
      continue;
    if (!selector_targets_return_zero(register_name, existing->second))
      continue;
    return SelectorPlan{
        .register_name = register_name,
        .selector_value = existing->second,
        .existing_preload = true,
    };
  }
  return std::nullopt;
}

std::optional<SelectorPlan>
new_return_zero_selector(const std::vector<IrOp>& ops,
                         const std::map<std::string, std::string>& preloaded) {
  for (const std::string_view register_view : kStableRegisters) {
    const std::string register_name(register_view);
    if (register_is_used(ops, register_name) || register_is_overwritten(ops, register_name) ||
        preloaded.contains(register_name))
      continue;
    const std::string selector_value(kReturnZeroSelector);
    if (!selector_targets_return_zero(register_name, selector_value))
      continue;
    return SelectorPlan{
        .register_name = register_name,
        .selector_value = selector_value,
        .existing_preload = false,
    };
  }
  return std::nullopt;
}

std::optional<SelectorPlan>
select_return_zero_selector(const std::vector<IrOp>& ops,
                            const std::map<std::string, std::string>& preloaded) {
  if (const std::optional<SelectorPlan> existing = existing_return_zero_selector(ops, preloaded))
    return existing;
  return new_return_zero_selector(ops, preloaded);
}

bool first_executable_op_is_return(const std::vector<IrOp>& ops) {
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label)
      continue;
    return op.kind == IrKind::Return;
  }
  return false;
}

std::vector<int> address_by_index(const std::vector<IrOp>& ops) {
  std::vector<int> addresses;
  addresses.reserve(ops.size());
  int address = 0;
  for (const IrOp& op : ops) {
    addresses.push_back(address);
    address += cells_per_op(op);
  }
  return addresses;
}

std::optional<int> numeric_flow_target(const IrOp& op) {
  if (op.kind != IrKind::Jump && op.kind != IrKind::Call && op.kind != IrKind::CondJump &&
      op.kind != IrKind::Loop) {
    return std::nullopt;
  }
  const auto* target = std::get_if<int>(&op.target);
  if (target == nullptr || *target < 0 || *target > 104)
    return std::nullopt;
  return *target;
}

int max_numeric_flow_target(const std::vector<IrOp>& ops) {
  int max_target = -1;
  for (const IrOp& op : ops) {
    const std::optional<int> target = numeric_flow_target(op);
    if (target.has_value() && *target > max_target)
      max_target = *target;
  }
  return max_target;
}

bool target_is_formal_address(const IrOp& op) {
  if (op.target_meta.formal_opcode.has_value())
    return true;
  return std::find(op.target_meta.roles.begin(), op.target_meta.roles.end(), "formal-address") !=
         op.target_meta.roles.end();
}

bool targets_return_zero(const IrOp& op, const std::map<std::string, int>& labels) {
  if (target_is_formal_address(op))
    return false;
  const std::optional<int> target = target_address(op.target, labels);
  return target.has_value() && *target == 0;
}

bool eligible_return_trampoline_site(const IrOp& op, const std::map<std::string, int>& labels,
                                     int site_address, int max_numeric_target) {
  if (has_rewrite_barrier(op) || (op.kind != IrKind::Jump && op.kind != IrKind::CondJump))
    return false;
  if (!targets_return_zero(op, labels))
    return false;
  return max_numeric_target <= site_address;
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

IrOp indirect_return_trampoline_op(const IrOp& op, const SelectorPlan& selector) {
  const int offset = register_index(selector.register_name);
  IrOp result = op;
  result.register_name = selector.register_name;
  result.target = 0;
  result.target_meta = {};
  if (op.kind == IrKind::Jump) {
    result.kind = IrKind::IndirectJump;
    result.opcode = 0x80 + offset;
    result.meta.mnemonic = "К БП " + selector.register_name;
  } else {
    result.kind = IrKind::IndirectCondJump;
    result.opcode = indirect_cond_base(op.condition) + offset;
    result.meta.mnemonic = "К " + indirect_cond_name(op.condition) + " " + selector.register_name;
  }
  result.meta = clone_meta(result.meta, "return trampoline R" + selector.register_name + "=" +
                                            selector.selector_value + " indirect-target=0");
  return result;
}

} // namespace

PassResult return_trampoline(const std::vector<IrOp>& ops, const PassContext& context) {
  if (!first_executable_op_is_return(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::map<std::string, int> labels = calculate_label_addresses(ops);
  const std::vector<int> addresses = address_by_index(ops);
  const int max_numeric_target = max_numeric_flow_target(ops);
  const std::optional<SelectorPlan> selector =
      select_return_zero_selector(ops, context.options.preloaded_constant_registers);
  if (!selector.has_value())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (!eligible_return_trampoline_site(op, labels, addresses.at(index), max_numeric_target)) {
      result.push_back(op);
      continue;
    }
    result.push_back(indirect_return_trampoline_op(op, *selector));
    ++applied;
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<PreloadReport> preloads;
  if (!selector->existing_preload) {
    preloads.push_back(PreloadReport{
        .register_name = selector->register_name,
        .value = selector->selector_value,
        .counts_against_program = false,
    });
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "return-trampoline",
                  .detail = "Replaced " + std::to_string(applied) +
                            " direct branch(es) to В/О@00 with one-cell indirect return "
                            "trampoline flow.",
              },
          },
      .preloads = std::move(preloads),
  };
}

IrPass return_trampoline_pass() {
  return IrPass{
      .name = "return-trampoline",
      .run = return_trampoline,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
