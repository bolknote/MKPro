#include "mkpro/core/passes/constant_folding.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

std::optional<int> digit_of(const IrOp& op) {
  if (op.kind == IrKind::Plain && op.opcode >= 0x00 && op.opcode <= 0x09)
    return op.opcode;
  return std::nullopt;
}

std::optional<std::string> alu_opcode(const IrOp& op) {
  if (op.kind != IrKind::Plain)
    return std::nullopt;
  if (op.opcode == 0x10)
    return "+";
  if (op.opcode == 0x11)
    return "-";
  if (op.opcode == 0x12)
    return "*";
  if (op.opcode == 0x13)
    return "/";
  return std::nullopt;
}

bool is_identity_plus(const IrOp& op, const IrOp* previous) {
  if (previous == nullptr)
    return false;
  const std::optional<int> digit = digit_of(*previous);
  if (!digit.has_value() || *digit != 0)
    return false;
  return alu_opcode(op) == "+";
}

bool is_identity_mul(const IrOp& op, const IrOp* previous) {
  if (previous == nullptr)
    return false;
  const std::optional<int> digit = digit_of(*previous);
  if (!digit.has_value() || *digit != 1)
    return false;
  return alu_opcode(op) == "*";
}

// X/Y/Z/T, the user-visible last-X register X1 used by F Bx, and the separate
// hidden decimal-entry X2 state restored by ./+/-/VP.
using SymbolicStackState = std::array<int, 6>;

constexpr std::array<int, 4> kSymbolicStackOpcodes{0x0d, 0x0e, 0x0f, 0x14};
constexpr int kSymbolicZero = 6;
constexpr std::size_t kMaximumSynthesizedLength = 3;
constexpr std::size_t kMaximumMatchedLength = 4;

bool apply_symbolic_stack_opcode(SymbolicStackState& state, int opcode) {
  const SymbolicStackState old = state;
  switch (opcode) {
  case 0x0d: // Cx
    state = {kSymbolicZero, old.at(1), old.at(2), old.at(3), old.at(4), old.at(0)};
    return true;
  case 0x0e: // B-up: X, X, Y, Z
    state = {old.at(0), old.at(0), old.at(1), old.at(2), old.at(4), old.at(0)};
    return true;
  case 0x0f: // F Bx: X1, X, Y, Z; X1 is preserved
    state = {old.at(4), old.at(0), old.at(1), old.at(2), old.at(4), old.at(0)};
    return true;
  case 0x14: // X <-> Y; old X is copied to X1, hidden X2 is preserved
    state = {old.at(1), old.at(0), old.at(2), old.at(3), old.at(0), old.at(5)};
    return true;
  default:
    return false;
  }
}

const std::map<SymbolicStackState, std::vector<int>>& shortest_symbolic_stack_sequences() {
  static const std::map<SymbolicStackState, std::vector<int>> sequences = [] {
    std::map<SymbolicStackState, std::vector<int>> result;
    std::function<void(const SymbolicStackState&, std::vector<int>&)> enumerate =
        [&](const SymbolicStackState& state, std::vector<int>& sequence) {
          const auto existing = result.find(state);
          if (existing == result.end() || sequence.size() < existing->second.size() ||
              (sequence.size() == existing->second.size() && sequence < existing->second)) {
            result.insert_or_assign(state, sequence);
          }
          if (sequence.size() == kMaximumSynthesizedLength)
            return;
          for (const int opcode : kSymbolicStackOpcodes) {
            SymbolicStackState next = state;
            if (!apply_symbolic_stack_opcode(next, opcode))
              continue;
            sequence.push_back(opcode);
            enumerate(next, sequence);
            sequence.pop_back();
          }
        };
    std::vector<int> sequence;
    enumerate(SymbolicStackState{0, 1, 2, 3, 4, 5}, sequence);
    return result;
  }();
  return sequences;
}

bool is_symbolic_stack_candidate(const IrOp& op) {
  if (op.kind != IrKind::Plain ||
      std::find(kSymbolicStackOpcodes.begin(), kSymbolicStackOpcodes.end(), op.opcode) ==
          kSymbolicStackOpcodes.end() ||
      has_rewrite_barrier(op) || is_display_focus_sensitive(op) || !op.meta.roles.empty() ||
      !op.target_meta.roles.empty() || !op.meta.semantic_call_origins.empty() ||
      op.meta.tactic.has_value() || op.procedure_boundary.has_value() || !op.name.empty() ||
      !op.register_name.empty() || !op.condition.empty() || !op.counter.empty() ||
      !op.semantic.empty() || op.meta.indirect_flow_targets.has_value() ||
      op.meta.indirect_memory_targets.has_value() || op.meta.logical_register_name.has_value() ||
      op.meta.logical_indirect_memory_targets.has_value() || op.meta.logical_register_analysis ||
      op.meta.discarded_indirect_recall_value || op.meta.borrowed_entry_phase_selector) {
    return false;
  }
  return true;
}

bool next_command_has_stable_entry_context(const std::vector<IrOp>& ops, std::size_t start) {
  const std::optional<int> next = next_executable_index(ops, static_cast<int>(start));
  if (!next.has_value())
    return false;
  const IrOp& op = ops.at(static_cast<std::size_t>(*next));
  if (op.kind == IrKind::OrphanAddress)
    return false;
  // Digits, decimal point, sign and VP inspect the previous-command state.
  // Exact X/Y/Z/T/X1 equivalence alone is therefore insufficient before them.
  return op.kind != IrKind::Plain || op.opcode < 0x00 || op.opcode > 0x0c;
}

std::optional<std::vector<int>> shorter_symbolic_stack_sequence(
    const std::vector<IrOp>& ops, std::size_t start, std::size_t length) {
  if (length < 2 || length > kMaximumMatchedLength || start + length > ops.size())
    return std::nullopt;
  const IrOp& first = ops.at(start);
  SymbolicStackState state{0, 1, 2, 3, 4, 5};
  for (std::size_t offset = 0; offset < length; ++offset) {
    const IrOp& op = ops.at(start + offset);
    if (!is_symbolic_stack_candidate(op) || op.procedure_name != first.procedure_name ||
        op.hidden != first.hidden || !apply_symbolic_stack_opcode(state, op.opcode)) {
      return std::nullopt;
    }
  }
  if (!next_command_has_stable_entry_context(ops, start + length))
    return std::nullopt;
  const auto replacement = shortest_symbolic_stack_sequences().find(state);
  if (replacement == shortest_symbolic_stack_sequences().end() ||
      replacement->second.empty() || replacement->second.size() >= length ||
      replacement->second.back() != ops.at(start + length - 1).opcode) {
    return std::nullopt;
  }
  return replacement->second;
}

IrOp synthesized_stack_op(int opcode, const IrOp& source) {
  IrOp result;
  result.kind = IrKind::Plain;
  result.procedure_name = source.procedure_name;
  result.hidden = source.hidden;
  result.opcode = opcode;
  result.meta.mnemonic = opcode_by_code(opcode).name;
  result.meta.source_line = source.meta.source_line;
  return result;
}

} // namespace

PassResult constant_folding(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int applied = 0;

  for (const IrOp& op : ops) {
    const IrOp* previous = result.empty() ? nullptr : &result.back();
    if ((is_identity_plus(op, previous) || is_identity_mul(op, previous)) && previous != nullptr &&
        previous->kind == IrKind::Plain && !previous->meta.raw && op.kind == IrKind::Plain &&
        !op.meta.raw) {
      result.pop_back();
      ++applied;
      continue;
    }
    result.push_back(op);
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          applied > 0
              ? std::vector<AppliedOptimization>{
                    AppliedOptimization{
                        .name = "constant-folding",
                        .detail = "Dropped " + std::to_string(applied) +
                                  " identity arithmetic operation(s) (0+ or 1*).",
                    },
                }
              : std::vector<AppliedOptimization>{},
  };
}

IrPass constant_folding_pass() {
  return IrPass{
      .name = "constant-folding",
      .run = constant_folding,
      .layout_safe = false,
  };
}

PassResult bounded_symbolic_superoptimizer(const std::vector<IrOp>& ops,
                                           const PassContext& context) {
  (void)context;

  struct Choice {
    std::size_t output_cells = std::numeric_limits<std::size_t>::max();
    std::size_t consumed = 1;
    std::vector<int> replacement;
  };

  std::vector<Choice> choices(ops.size());
  std::vector<std::size_t> minimum_cells(ops.size() + 1, 0);
  for (std::size_t reverse = ops.size(); reverse > 0; --reverse) {
    const std::size_t index = reverse - 1;
    choices.at(index).output_cells = 1 + minimum_cells.at(index + 1);
    for (std::size_t length = 2; length <= kMaximumMatchedLength; ++length) {
      const std::optional<std::vector<int>> replacement =
          shorter_symbolic_stack_sequence(ops, index, length);
      if (!replacement.has_value())
        continue;
      const std::size_t candidate_cells =
          replacement->size() + minimum_cells.at(index + length);
      if (candidate_cells >= choices.at(index).output_cells)
        continue;
      choices.at(index) = Choice{
          .output_cells = candidate_cells,
          .consumed = length,
          .replacement = *replacement,
      };
    }
    minimum_cells.at(index) = choices.at(index).output_cells;
  }

  std::vector<IrOp> result;
  result.reserve(minimum_cells.front());
  int applied = 0;
  std::size_t saved_cells = 0;
  for (std::size_t index = 0; index < ops.size();) {
    const Choice& choice = choices.at(index);
    if (choice.consumed == 1) {
      result.push_back(ops.at(index));
      ++index;
      continue;
    }
    for (const int opcode : choice.replacement)
      result.push_back(synthesized_stack_op(opcode, ops.at(index)));
    saved_cells += choice.consumed - choice.replacement.size();
    ++applied;
    index += choice.consumed;
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          applied > 0
              ? std::vector<AppliedOptimization>{AppliedOptimization{
                    .name = "bounded-symbolic-superoptimizer",
                    .detail = "Replaced " + std::to_string(applied) +
                              " straight-line stack/X1/X2 region(s), saving " +
                              std::to_string(saved_cells) + " cell(s).",
                }}
              : std::vector<AppliedOptimization>{},
  };
}

IrPass bounded_symbolic_superoptimizer_pass() {
  return IrPass{
      .name = "bounded-symbolic-superoptimizer",
      .run = bounded_symbolic_superoptimizer,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
