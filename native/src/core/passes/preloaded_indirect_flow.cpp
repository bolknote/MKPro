#include "mkpro/core/passes/preloaded_indirect_flow.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr std::array<std::string_view, 8> kStableRegisters = {
    "7", "8", "9", "a", "b", "c", "d", "e",
};

struct SelectorPlan {
  std::string register_name;
  std::string selector_value;
  bool super_dark = false;
  bool existing_constant = false;
};

struct EligibleTarget {
  int target = 0;
  std::string selector_value;
  bool super_dark = false;
  std::vector<int> indices;
};

struct RuntimeCallTarget {
  int target = 0;
  std::vector<int> indices;
  int insert_index = 0;
  int insert_address = 0;
};

struct RuntimeCallPlan {
  int target = 0;
  std::string register_name;
  std::set<int> indices;
  int insert_index = 0;
};

IrMeta clone_meta(IrMeta meta, const std::string& comment) {
  if (meta.comment.has_value() && !meta.comment->empty()) {
    meta.comment = *meta.comment + "; " + comment;
  } else {
    meta.comment = comment;
  }
  return meta;
}

bool has_role(const std::vector<CellRole>& roles, std::string_view role) {
  return std::find(roles.begin(), roles.end(), role) != roles.end();
}

bool call_consumes_current_x(const IrOp& op) {
  return has_role(op.meta.roles, "x-argument-call") ||
         has_role(op.meta.roles, "statement-proc-call");
}

void restore_statement_proc_call_comment(IrMeta& meta) {
  constexpr std::string_view kCallFunctionPrefix = "call function";
  if (!has_role(meta.roles, "statement-proc-call") || !meta.comment.has_value() ||
      !meta.comment->starts_with(kCallFunctionPrefix)) {
    return;
  }
  meta.comment = "proc call" + meta.comment->substr(kCallFunctionPrefix.size());
}

std::set<std::string> used_registers(const std::vector<IrOp>& ops) {
  std::set<std::string> used;
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Store || op.kind == IrKind::Recall ||
        op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall ||
        op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
        op.kind == IrKind::IndirectCondJump) {
      used.insert(op.register_name);
    }
  }
  return used;
}

std::set<std::string> reserved_preloaded_registers(
    const std::map<std::string, std::string>& preloaded) {
  std::set<std::string> reserved;
  for (const auto& [register_name, value] : preloaded) {
    (void)value;
    reserved.insert(register_name);
  }
  return reserved;
}

std::vector<std::string> spare_stable_registers(const std::vector<IrOp>& ops,
                                                const std::set<std::string>& reserved = {}) {
  const std::set<std::string> used = used_registers(ops);
  std::vector<std::string> result;
  for (const std::string_view register_name : kStableRegisters) {
    const std::string candidate(register_name);
    if (!used.contains(candidate) && !reserved.contains(candidate))
      result.push_back(candidate);
  }
  return result;
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

std::map<int, SelectorPlan> existing_constant_selector_plans(
    const std::vector<IrOp>& ops, const std::map<std::string, std::string>& preloaded) {
  std::map<int, SelectorPlan> plans;
  for (const std::string_view register_view : kStableRegisters) {
    const std::string register_name(register_view);
    const auto value = preloaded.find(register_name);
    if (value == preloaded.end() || register_is_overwritten(ops, register_name))
      continue;
    const std::optional<mkpro::core::IndirectAddressEvaluation> evaluated =
        mkpro::core::evaluate_indirect_address(register_name, value->second,
                                               mkpro::core::IndirectOperationKind::Flow);
    if (!evaluated.has_value() || !evaluated->actual_flow_target.has_value())
      continue;
    const int target = *evaluated->actual_flow_target;
    if (target < 0 || target > 104 || plans.contains(target))
      continue;
    plans[target] = SelectorPlan{
        .register_name = register_name,
        .selector_value = value->second,
        .super_dark =
            evaluated->super_dark.has_value() && evaluated->super_dark->entry_address == target,
        .existing_constant = true,
    };
  }
  return plans;
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

std::optional<int> branch_target(const IrOp& op) {
  if (op.kind != IrKind::Jump && op.kind != IrKind::Call && op.kind != IrKind::CondJump)
    return std::nullopt;
  if (op.target_meta.formal_opcode.has_value())
    return std::nullopt;
  if (std::find(op.target_meta.roles.begin(), op.target_meta.roles.end(), "formal-address") !=
      op.target_meta.roles.end()) {
    return std::nullopt;
  }
  return numeric_flow_target(op);
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

const IrOp* op_at_address(const std::vector<IrOp>& ops, const std::vector<int>& addresses,
                          int address) {
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (cells_per_op(op) == 0)
      continue;
    if (addresses.at(index) == address)
      return &op;
  }
  return nullptr;
}

bool has_single_cell_fallthrough(const IrOp* op) {
  if (op == nullptr || cells_per_op(*op) != 1)
    return false;
  return op->kind == IrKind::Plain || op->kind == IrKind::Store ||
         op->kind == IrKind::Recall || op->kind == IrKind::IndirectStore ||
         op->kind == IrKind::IndirectRecall || op->kind == IrKind::Stop;
}

bool is_super_dark_compatible_target(const std::vector<IrOp>& ops,
                                     const std::vector<int>& addresses,
                                     const std::map<std::string, int>& labels, int target) {
  if (target < 48 || target > 53)
    return false;
  const IrOp* entry = op_at_address(ops, addresses, target);
  if (!has_single_cell_fallthrough(entry))
    return false;

  const int continuation_address = target - 47;
  const IrOp* after_entry = op_at_address(ops, addresses, target + 1);
  if (after_entry == nullptr || after_entry->kind != IrKind::Jump)
    return false;
  const std::optional<int> resolved = target_address(after_entry->target, labels);
  return resolved.has_value() && *resolved == continuation_address;
}

std::string uppercase_hex_digit(int value) {
  if (value < 10)
    return std::to_string(value);
  return std::string(1, static_cast<char>('A' + (value - 10)));
}

std::string formal_label_from_ordinal(int ordinal) {
  return uppercase_hex_digit(ordinal / 10) + uppercase_hex_digit(ordinal % 10);
}

std::string formal_label_from_opcode(int opcode) {
  return uppercase_hex_digit(opcode / 16) + uppercase_hex_digit(opcode % 16);
}

std::string official_label(int target) {
  if (target <= 99)
    return std::to_string(target / 10) + std::to_string(target % 10);
  return "A" + std::to_string(target - 100);
}

SelectorPlan selector_for_target(const std::vector<IrOp>& ops, const std::vector<int>& addresses,
                                 const std::map<std::string, int>& labels, int target) {
  if (is_super_dark_compatible_target(ops, addresses, labels, target)) {
    return SelectorPlan{
        .selector_value = formal_label_from_opcode(0xfa + (target - 48)),
        .super_dark = true,
    };
  }
  // Dark formal aliases (formal_label_from_ordinal) for targets 0..47 decode to
  // the same address as the plain decimal, but they are delivered as a register
  // preload that the runtime/test harness loads RAW (as a number). Only the
  // B/C/D-prefixed aliases (targets 0..27) survive that raw load: an E-prefixed
  // alias ("E0".."E9") parses as exponent notation and throws, and an
  // F-prefixed alias ("F0".."F9") has leading BCD nibble 15 that normalizes away
  // (e.g. "F6" -> 6), so the delivered selector would jump to the wrong address.
  // For targets 28..47 fall back to the raw-stable plain decimal address.
  if (target <= 27) {
    return SelectorPlan{
        .selector_value = formal_label_from_ordinal(target + 112),
        .super_dark = false,
    };
  }
  return SelectorPlan{
      .selector_value = official_label(target),
      .super_dark = false,
  };
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

IrOp indirect_flow_op(const IrOp& op, const std::string& register_name,
                      const std::string& selector_value, int target, bool super_dark) {
  const int offset = register_index(register_name);
  const std::string suffix = "preloaded R" + register_name + "=" + selector_value +
                             " indirect-target=" + std::to_string(target) +
                             (super_dark ? " super-dark" : "") + " indirect flow";
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
  if (result.kind == IrKind::IndirectCall)
    restore_statement_proc_call_comment(result.meta);
  result.meta = clone_meta(result.meta, suffix);
  return result;
}

std::vector<IrOp> runtime_selector_literal_ops(int target, const std::string& register_name) {
  std::vector<IrOp> result;
  const std::string text = std::to_string(target);
  result.reserve(text.size() + 1U);
  for (const char digit : text) {
    IrOp op;
    op.kind = IrKind::Plain;
    op.opcode = digit - '0';
    op.meta.mnemonic = std::string(1, digit);
    op.meta.comment = "runtime indirect call selector " + text;
    result.push_back(std::move(op));
  }
  IrOp store;
  store.kind = IrKind::Store;
  store.register_name = register_name;
  store.opcode = 0x40 + register_index(register_name);
  store.meta.mnemonic = "X->П " + register_name;
  store.meta.comment = "runtime indirect call selector " + text;
  result.push_back(std::move(store));
  return result;
}

bool op_references_register(const IrOp& op, const std::string& register_name) {
  if (op.kind == IrKind::Store || op.kind == IrKind::Recall ||
      op.kind == IrKind::IndirectStore || op.kind == IrKind::IndirectRecall ||
      op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
      op.kind == IrKind::IndirectCondJump) {
    return op.register_name == register_name;
  }
  return false;
}

std::optional<int> first_op_index_at_address(const std::vector<IrOp>& ops,
                                             const std::vector<int>& addresses, int address) {
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (cells_per_op(ops.at(index)) == 0)
      continue;
    if (addresses.at(index) == address)
      return static_cast<int>(index);
  }
  return std::nullopt;
}

bool can_borrow_register_for_runtime_selector(const std::vector<IrOp>& ops,
                                              const std::string& register_name, int target_index,
                                              int insert_index, const std::set<int>& selected) {
  for (int index = target_index; index < static_cast<int>(ops.size()); ++index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (has_rewrite_barrier(op))
      return false;
    if (selected.contains(index))
      continue;
    if (index >= insert_index && op_references_register(op, register_name))
      return false;
    if (index < insert_index && op_references_register(op, register_name))
      return false;
  }
  return true;
}

std::vector<RuntimeCallPlan> runtime_indirect_call_plans(
    const std::vector<IrOp>& ops, bool break_even, const std::set<std::string>& reserved) {
  const std::vector<int> addresses = address_by_index(ops);
  const std::map<std::string, int> labels = calculate_label_addresses(ops);
  std::map<int, RuntimeCallTarget> targets;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (has_rewrite_barrier(op) || op.kind != IrKind::Call || call_consumes_current_x(op))
      continue;
    if (op.target_meta.formal_opcode.has_value() ||
        std::find(op.target_meta.roles.begin(), op.target_meta.roles.end(), "formal-address") !=
            op.target_meta.roles.end()) {
      continue;
    }
    const std::optional<int> target = target_address(op.target, labels);
    const int site_address = addresses.at(index);
    if (!target.has_value() || *target < 0 || *target > 99 || *target >= site_address)
      continue;
    RuntimeCallTarget& entry = targets[*target];
    if (entry.indices.empty()) {
      entry.target = *target;
      entry.insert_index = static_cast<int>(index);
      entry.insert_address = site_address;
    }
    entry.indices.push_back(static_cast<int>(index));
  }

  std::vector<RuntimeCallTarget> sorted;
  sorted.reserve(targets.size());
  for (const auto& [target, entry] : targets) {
    (void)target;
    sorted.push_back(entry);
  }
  std::sort(sorted.begin(), sorted.end(), [](const RuntimeCallTarget& left,
                                             const RuntimeCallTarget& right) {
    if (left.indices.size() != right.indices.size())
      return left.indices.size() > right.indices.size();
    return left.insert_address < right.insert_address;
  });

  std::vector<RuntimeCallPlan> plans;
  std::set<std::string> used_registers;
  for (const RuntimeCallTarget& candidate : sorted) {
    const int preload_cost = static_cast<int>(std::to_string(candidate.target).size()) + 1;
    const int margin = break_even ? 0 : 2;
    if (static_cast<int>(candidate.indices.size()) <= preload_cost + margin)
      continue;
    const std::set<int> selected(candidate.indices.begin(), candidate.indices.end());
    const std::optional<int> target_index =
        first_op_index_at_address(ops, addresses, candidate.target);
    if (!target_index.has_value())
      continue;
    auto register_it = std::find_if(kStableRegisters.begin(), kStableRegisters.end(),
                                    [&](std::string_view item) {
                                      const std::string candidate_register(item);
                                      return !used_registers.contains(candidate_register) &&
                                             !reserved.contains(candidate_register) &&
                                             can_borrow_register_for_runtime_selector(
                                                 ops, candidate_register, *target_index,
                                                 candidate.insert_index, selected);
                                    });
    if (register_it == kStableRegisters.end())
      continue;
    const std::string register_name(*register_it);
    used_registers.insert(register_name);
    plans.push_back(RuntimeCallPlan{
        .target = candidate.target,
        .register_name = register_name,
        .indices = selected,
        .insert_index = candidate.insert_index,
    });
  }
  return plans;
}

bool contains_formal_alias(const std::string& value) {
  return std::any_of(value.begin(), value.end(), [](char ch) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return upper >= 'B' && upper <= 'F';
  });
}

} // namespace

PassResult runtime_indirect_call_flow(const std::vector<IrOp>& ops, const PassContext& context) {
  // Post-parity optimization (candidate #6): previously the runtime indirect-call
  // selector rewrite was suppressed for non-hoisted, non-explicit test-only lowering
  // variants (disable_candidate_search) so the variant fingerprints matched the
  // TypeScript oracle's direct-call form. The rewrite already runs (and is exercised
  // behaviorally) in the default candidate-search pipeline; lifting the parity gate
  // simply lets the primary/non-hoisted variants surface their natural shorter form.
  const std::vector<RuntimeCallPlan> plans = runtime_indirect_call_plans(
      ops, context.options.aggressive_indirect_call_threshold,
      reserved_preloaded_registers(context.options.preloaded_constant_registers));
  if (plans.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<int, std::vector<RuntimeCallPlan>> by_insert;
  std::map<int, RuntimeCallPlan> by_index;
  for (const RuntimeCallPlan& plan : plans) {
    by_insert[plan.insert_index].push_back(plan);
    for (const int index : plan.indices)
      by_index[index] = plan;
  }

  std::vector<IrOp> result;
  int rewritten = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const int signed_index = static_cast<int>(index);
    for (const RuntimeCallPlan& plan : by_insert[signed_index]) {
      std::vector<IrOp> selector_ops = runtime_selector_literal_ops(plan.target, plan.register_name);
      result.insert(result.end(), selector_ops.begin(), selector_ops.end());
    }
    const auto plan = by_index.find(signed_index);
    const IrOp& op = ops.at(index);
    if (plan != by_index.end() && op.kind == IrKind::Call) {
      result.push_back(indirect_flow_op(op, plan->second.register_name,
                                        std::to_string(plan->second.target), plan->second.target,
                                        false));
      ++rewritten;
      continue;
    }
    result.push_back(op);
  }

  return PassResult{
      .ops = std::move(result),
      .applied = rewritten,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "runtime-indirect-call-flow",
                  .detail = "Borrowed dead stable register(s) for " +
                            std::to_string(rewritten) + " repeated direct helper call(s).",
              },
          },
  };
}

IrPass runtime_indirect_call_flow_pass() {
  return IrPass{
      .name = "runtime-indirect-call-flow",
      .run = runtime_indirect_call_flow,
      .layout_safe = false,
  };
}

PassResult run_preloaded_indirect_flow(const std::vector<IrOp>& ops,
                                       const PassContext& context,
                                       const IndirectFlowOptions& flow_options) {
  const std::set<std::string> reserved =
      reserved_preloaded_registers(context.options.preloaded_constant_registers);
  std::vector<std::string> registers = spare_stable_registers(ops, reserved);
  const std::map<int, SelectorPlan> existing_selectors =
      context.options.dual_use_constant_indirect_flow
          ? existing_constant_selector_plans(ops, context.options.preloaded_constant_registers)
          : std::map<int, SelectorPlan>{};
  if (registers.empty() && existing_selectors.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<int> addresses = address_by_index(ops);
  const std::map<std::string, int> labels = calculate_label_addresses(ops);
  const int max_target = max_numeric_flow_target(ops);
  std::map<int, EligibleTarget> eligible_targets;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (has_rewrite_barrier(op) ||
        (op.kind != IrKind::Jump && op.kind != IrKind::Call && op.kind != IrKind::CondJump)) {
      continue;
    }
    const std::optional<int> target = branch_target(op);
    const int site_address = addresses.at(index);
    if (!target.has_value() ||
        (!flow_options.allow_forward_targets && *target > site_address) ||
        (!flow_options.relax_max_target_guard && max_target > site_address)) {
      continue;
    }

    const SelectorPlan selected_target = selector_for_target(ops, addresses, labels, *target);
    const auto existing_selector = existing_selectors.find(*target);
    std::optional<mkpro::core::IndirectAddressEvaluation> evaluated;
    if (!registers.empty()) {
      evaluated = mkpro::core::evaluate_indirect_address(
          registers.front(), selected_target.selector_value,
          mkpro::core::IndirectOperationKind::Flow);
    }
    const bool selected_super_dark =
        evaluated.has_value() && evaluated->super_dark.has_value() &&
        evaluated->super_dark->entry_address == *target;
    if (existing_selector == existing_selectors.end() &&
        (!evaluated.has_value() || evaluated->actual_flow_target != *target ||
         selected_super_dark != selected_target.super_dark)) {
      continue;
    }

    EligibleTarget& entry = eligible_targets[*target];
    if (entry.indices.empty()) {
      entry.target = *target;
      entry.selector_value = selected_target.selector_value;
      entry.super_dark = selected_target.super_dark;
    }
    entry.indices.push_back(static_cast<int>(index));
  }

  std::vector<EligibleTarget> sorted_targets;
  sorted_targets.reserve(eligible_targets.size());
  for (const auto& [target, entry] : eligible_targets) {
    (void)target;
    sorted_targets.push_back(entry);
  }
  std::sort(sorted_targets.begin(), sorted_targets.end(), [](const EligibleTarget& left,
                                                             const EligibleTarget& right) {
    if (left.indices.size() != right.indices.size())
      return left.indices.size() > right.indices.size();
    return left.indices.front() < right.indices.front();
  });

  std::map<int, SelectorPlan> targets;
  std::vector<PreloadReport> preloads;
  int reused_existing_constants = 0;
  std::set<std::string> used_existing_registers;
  std::size_t next_register = 0;

  for (const EligibleTarget& target : sorted_targets) {
    const auto existing_selector = existing_selectors.find(target.target);
    if (existing_selector != existing_selectors.end() &&
        !used_existing_registers.contains(existing_selector->second.register_name)) {
      targets[target.target] = existing_selector->second;
      used_existing_registers.insert(existing_selector->second.register_name);
      ++reused_existing_constants;
      continue;
    }
    if (next_register >= registers.size())
      break;
    const std::string register_name = registers.at(next_register);
    ++next_register;
    if (!mkpro::core::is_stable_indirect_selector(register_name))
      continue;
    targets[target.target] = SelectorPlan{
        .register_name = register_name,
        .selector_value = target.selector_value,
        .super_dark = target.super_dark,
    };
    preloads.push_back(PreloadReport{
        .register_name = register_name,
        .value = target.selector_value,
        .counts_against_program = false,
    });
  }

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;
  int super_dark_applied = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (has_rewrite_barrier(op) ||
        (op.kind != IrKind::Jump && op.kind != IrKind::Call && op.kind != IrKind::CondJump)) {
      result.push_back(op);
      continue;
    }
    const std::optional<int> target = branch_target(op);
    const int site_address = addresses.at(index);
    if (!target.has_value() ||
        (!flow_options.allow_forward_targets && *target > site_address) ||
        (!flow_options.relax_max_target_guard && max_target > site_address)) {
      result.push_back(op);
      continue;
    }

    const auto selected = targets.find(*target);
    if (selected == targets.end()) {
      result.push_back(op);
      continue;
    }

    result.push_back(indirect_flow_op(op, selected->second.register_name,
                                      selected->second.selector_value, *target,
                                      selected->second.super_dark));
    ++applied;
    if (selected->second.super_dark)
      ++super_dark_applied;
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const int formal = static_cast<int>(std::count_if(preloads.begin(), preloads.end(),
                                                    [](const PreloadReport& preload) {
                                                      return contains_formal_alias(preload.value);
                                                    }));
  const std::string reused =
      reused_existing_constants == 0
          ? ""
          : " and reused " + std::to_string(reused_existing_constants) +
                " existing constant selector" + (reused_existing_constants == 1 ? "" : "s");

  std::vector<AppliedOptimization> optimizations = {
      AppliedOptimization{
          .name = "preloaded-indirect-flow",
          .detail = "Replaced " + std::to_string(applied) +
                    " numeric direct branch/call(s) with compiler-owned preloaded indirect flow (" +
                    std::to_string(formal) + " formal alias selector" +
                    (formal == 1 ? "" : "s") + ")" + reused + ".",
      },
  };
  if (super_dark_applied > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "preloaded-super-dark-flow",
        .detail = "Selected " + std::to_string(super_dark_applied) +
                  " FA..FF one-command indirect dispatch(es) after proving the entry cell falls "
                  "through to the matching 01..06 continuation jump.",
    });
  }
  if (reused_existing_constants > 0) {
    optimizations.push_back(AppliedOptimization{
        .name = "constants-dual-use",
        .detail = "Reused " + std::to_string(reused_existing_constants) +
                  " setup constant preload" + (reused_existing_constants == 1 ? "" : "s") +
                  " as stable indirect-flow selector" +
                  (reused_existing_constants == 1 ? "" : "s") + ".",
    });
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations = std::move(optimizations),
      .preloads = std::move(preloads),
  };
}

PassResult preloaded_indirect_flow(const std::vector<IrOp>& ops, const PassContext& context) {
  return run_preloaded_indirect_flow(ops, context, {});
}

IrPass preloaded_indirect_flow_pass() {
  return IrPass{
      .name = "preloaded-indirect-flow",
      .run = preloaded_indirect_flow,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
