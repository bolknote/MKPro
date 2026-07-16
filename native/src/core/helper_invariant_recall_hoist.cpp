#include "mkpro/core/helper_invariant_recall_hoist.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core {

namespace {

constexpr int kDirectCallOpcode = 0x53;
constexpr int kReturnOpcode = 0x52;
constexpr int kStopOpcode = 0x50;
constexpr int kDirectJumpOpcode = 0x51;
constexpr int kAndOpcode = 0x37;
constexpr int kOrOpcode = 0x38;
constexpr int kFirstDirectStoreOpcode = 0x40;
constexpr int kLastDirectStoreOpcode = 0x4e;
constexpr int kFirstDirectRecallOpcode = 0x60;
constexpr int kLastDirectRecallOpcode = 0x6e;
constexpr int kRegisterCount = 15;

struct ArtifactIndex {
  std::vector<int> item_addresses;
  std::map<std::string, std::size_t> label_items;
  std::map<std::string, int> label_addresses;
  std::set<std::string> duplicate_labels;
  std::map<int, std::size_t> cell_items;
  int cells = 0;
};

ArtifactIndex index_artifact(const std::vector<MachineItem>& items) {
  ArtifactIndex index;
  index.item_addresses.resize(items.size(), 0);
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    index.item_addresses.at(item_index) = address;
    if (item.kind == MachineItemKind::Label) {
      if (index.label_items.contains(item.name))
        index.duplicate_labels.insert(item.name);
      index.label_items[item.name] = item_index;
      index.label_addresses[item.name] = address;
    } else {
      index.cell_items[address] = item_index;
      ++address;
    }
  }
  index.cells = address;
  return index;
}

void add_reason(HelperInvariantRecallHoistProof& proof, std::string reason) {
  if (std::find(proof.reasons.begin(), proof.reasons.end(), reason) == proof.reasons.end())
    proof.reasons.push_back(std::move(reason));
}

bool is_direct_recall(int opcode) {
  return opcode >= kFirstDirectRecallOpcode && opcode <= kLastDirectRecallOpcode;
}

bool is_direct_store(int opcode) {
  return opcode >= kFirstDirectStoreOpcode && opcode <= kLastDirectStoreOpcode;
}

bool is_direct_register_alias(int opcode) {
  return opcode == 0x4f || opcode == 0x6f;
}

bool is_commutative_join(int opcode) {
  return opcode == kAndOpcode || opcode == kOrOpcode;
}

bool is_any_indirect_opcode(int opcode) {
  const int high = opcode & 0xf0;
  return high >= 0x70 && high <= 0xe0;
}

bool is_indirect_flow_opcode(int opcode) {
  const int high = opcode & 0xf0;
  return high == 0x70 || high == 0x80 || high == 0x90 || high == 0xa0 || high == 0xc0 ||
         high == 0xe0;
}

bool is_flow_opcode(int opcode) {
  if (opcode == kStopOpcode || opcode == kDirectJumpOpcode || opcode == kReturnOpcode ||
      opcode == kDirectCallOpcode || is_indirect_flow_opcode(opcode)) {
    return true;
  }
  return opcode >= 0x57 && opcode <= 0x5e;
}

bool opcode_accesses_register(int opcode, int register_index) {
  if (is_direct_register_alias(opcode))
    return register_index == 0;
  if (is_direct_store(opcode))
    return opcode - kFirstDirectStoreOpcode == register_index;
  if (is_direct_recall(opcode))
    return opcode - kFirstDirectRecallOpcode == register_index;
  if (is_any_indirect_opcode(opcode))
    return (opcode & 0x0f) == register_index;
  return false;
}

std::optional<std::size_t> previous_cell_item(const std::vector<MachineItem>& items,
                                              std::size_t before) {
  while (before > 0) {
    --before;
    if (items.at(before).kind != MachineItemKind::Label)
      return before;
  }
  return std::nullopt;
}

bool has_fallthrough_barrier_before(const std::vector<MachineItem>& items,
                                    std::size_t label_item_index) {
  const std::optional<std::size_t> previous = previous_cell_item(items, label_item_index);
  if (!previous.has_value())
    return false;
  const MachineItem& cell = items.at(*previous);
  if (cell.kind == MachineItemKind::Op)
    return cell.opcode == kStopOpcode || cell.opcode == kReturnOpcode ||
           (cell.opcode & 0xf0) == 0x80;
  if (cell.kind != MachineItemKind::Address || *previous == 0)
    return false;
  const MachineItem& flow = items.at(*previous - 1U);
  return flow.kind == MachineItemKind::Op && flow.opcode == kDirectJumpOpcode;
}

struct SymbolicState {
  std::array<std::string, 4> stack;
  std::string x2;
  std::array<std::string, kRegisterCount> registers;
};

SymbolicState initial_symbolic_state() {
  SymbolicState result;
  result.stack = {"stack.X", "stack.Y", "stack.Z", "stack.T"};
  result.x2 = "stack.X2";
  for (int index = 0; index < kRegisterCount; ++index)
    result.registers.at(static_cast<std::size_t>(index)) = "reg." + std::to_string(index);
  return result;
}

std::string unary_expression(int opcode, const std::string& value) {
  return "u" + std::to_string(opcode) + "(" + value + ")";
}

std::string binary_expression(int opcode, std::string left, std::string right) {
  if (is_commutative_join(opcode) && right < left)
    std::swap(left, right);
  return "b" + std::to_string(opcode) + "(" + left + "," + right + ")";
}

bool states_equal(const SymbolicState& left, const SymbolicState& right) {
  return left.stack == right.stack && left.x2 == right.x2 && left.registers == right.registers;
}

bool observable_values_equal(const SymbolicState& left, const SymbolicState& right) {
  return left.stack == right.stack && left.registers == right.registers;
}

bool execute_symbolic_op(SymbolicState& state, const MachineItem& item, bool allow_return,
                         std::string& rejection) {
  if (item.kind != MachineItemKind::Op || item.raw) {
    rejection = "symbolic transfer encountered a raw or non-opcode cell";
    return false;
  }
  const int opcode = item.opcode;
  const std::array<std::string, 4> old = state.stack;

  if (is_direct_register_alias(opcode)) {
    rejection = "symbolic transfer encountered an undocumented direct-register alias";
    return false;
  }

  if (is_direct_store(opcode)) {
    state.registers.at(static_cast<std::size_t>(opcode - kFirstDirectStoreOpcode)) = old.at(0);
    return true;
  }
  if (is_direct_recall(opcode)) {
    state.stack = {state.registers.at(static_cast<std::size_t>(opcode - kFirstDirectRecallOpcode)),
                   old.at(0), old.at(1), old.at(2)};
    state.x2 = old.at(0);
    return true;
  }
  if (opcode == kReturnOpcode && allow_return) {
    state.x2 = old.at(0);
    return true;
  }
  if (is_flow_opcode(opcode) || is_any_indirect_opcode(opcode)) {
    rejection = "symbolic transfer encountered control flow or indirect memory";
    return false;
  }

  // Stack-only commands whose metadata is intentionally conservative.
  if (opcode == 0x0d) { // Cx
    state.stack.at(0) = "zero";
    state.x2 = old.at(0);
    return true;
  }
  if (opcode == 0x0e) { // B-up: X, X, Y, Z
    state.stack = {old.at(0), old.at(0), old.at(1), old.at(2)};
    state.x2 = old.at(0);
    return true;
  }
  if (opcode == 0x0f) { // F Bx: Y, Z, T, T
    state.stack = {old.at(1), old.at(2), old.at(3), old.at(3)};
    state.x2 = old.at(0);
    return true;
  }
  if (opcode == 0x14) { // X <-> Y
    state.stack = {old.at(1), old.at(0), old.at(2), old.at(3)};
    return true;
  }

  const OpcodeInfo& info = opcode_by_code(opcode);
  if (info.takes_address || info.stack_effect == StackEffect::Barrier ||
      info.stack_effect == StackEffect::Unknown || info.x2_effect == X2Effect::Restores ||
      info.x2_effect == X2Effect::Unknown) {
    rejection = "symbolic transfer encountered an unsupported stack/X2 effect";
    return false;
  }

  switch (info.stack_effect) {
  case StackEffect::Preserves:
    state.stack.at(0) = unary_expression(opcode, old.at(0));
    break;
  case StackEffect::Shifts:
    state.stack = {"constant." + std::to_string(opcode), old.at(0), old.at(1), old.at(2)};
    break;
  case StackEffect::ConsumeYDrop:
    state.stack = {binary_expression(opcode, old.at(1), old.at(0)), old.at(2), old.at(3),
                   old.at(3)};
    break;
  case StackEffect::ConsumeYKeep:
    state.stack = {binary_expression(opcode, old.at(1), old.at(0)), old.at(1), old.at(2),
                   old.at(3)};
    break;
  case StackEffect::Exposes:
    state.stack = {old.at(1), old.at(2), old.at(3), old.at(3)};
    break;
  case StackEffect::Barrier:
  case StackEffect::Unknown:
    rejection = "symbolic transfer encountered an unsupported stack effect";
    return false;
  }

  if (info.x2_effect == X2Effect::Affects)
    state.x2 = old.at(0);
  return true;
}

bool same_evaluated_operands(const SymbolicState& left, const SymbolicState& right, int opcode,
                             bool allow_commutative) {
  if (opcode == 0x0d || opcode == 0x0e || opcode == 0x0f || opcode == 0x14 ||
      is_direct_store(opcode) || is_direct_recall(opcode)) {
    return true;
  }
  const OpcodeInfo& info = opcode_by_code(opcode);
  switch (info.stack_effect) {
  case StackEffect::Preserves:
    return left.stack.at(0) == right.stack.at(0);
  case StackEffect::ConsumeYDrop:
  case StackEffect::ConsumeYKeep:
    if (left.stack.at(0) == right.stack.at(0) && left.stack.at(1) == right.stack.at(1))
      return true;
    return allow_commutative && is_commutative_join(opcode) &&
           left.stack.at(0) == right.stack.at(1) && left.stack.at(1) == right.stack.at(0);
  case StackEffect::Shifts:
  case StackEffect::Exposes:
    return true;
  case StackEffect::Barrier:
  case StackEffect::Unknown:
    return false;
  }
  return false;
}

bool execute_symbolic_pair(SymbolicState& left, SymbolicState& right, const MachineItem& item,
                           bool allow_return, bool allow_commutative, std::string& rejection) {
  if (item.kind != MachineItemKind::Op || item.raw) {
    rejection = "paired symbolic transfer encountered a raw or non-opcode cell";
    return false;
  }
  if (item.opcode != kReturnOpcode &&
      !same_evaluated_operands(left, right, item.opcode, allow_commutative)) {
    rejection = "symbolic operands diverge before an evaluating command";
    return false;
  }
  std::string left_rejection;
  std::string right_rejection;
  if (!execute_symbolic_op(left, item, allow_return, left_rejection) ||
      !execute_symbolic_op(right, item, allow_return, right_rejection)) {
    rejection = !left_rejection.empty() ? left_rejection : right_rejection;
    return false;
  }
  return true;
}

struct ContinuationProof {
  bool proved = false;
  std::size_t cells = 0;
  std::string reason;
};

ContinuationProof prove_continuation(const std::vector<MachineItem>& items,
                                     std::size_t start_item_index, SymbolicState original,
                                     SymbolicState rewritten,
                                     const HelperInvariantRecallHoistOptions& options) {
  ContinuationProof proof;
  bool numeric_entry_active = false;
  for (std::size_t item_index = start_item_index; item_index < items.size(); ++item_index) {
    if (states_equal(original, rewritten)) {
      proof.proved = true;
      return proof;
    }
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label)
      continue;
    if (proof.cells >= options.max_continuation_cells) {
      proof.reason = "symbolic continuation exceeds the configured bound";
      return proof;
    }
    ++proof.cells;
    if (item.kind != MachineItemKind::Op || item.raw) {
      proof.reason = "symbolic continuation reaches a raw/address cell before convergence";
      return proof;
    }

    const OpcodeInfo& info = opcode_by_code(item.opcode);
    if (info.x2_effect == X2Effect::Restores && original.x2 != rewritten.x2) {
      proof.reason = "X2 is observed before a proved overwrite";
      return proof;
    }

    if (item.opcode >= 0x00 && item.opcode <= 0x09) {
      const std::array<std::string, 4> old_original = original.stack;
      const std::array<std::string, 4> old_rewritten = rewritten.stack;
      const std::string digit = std::to_string(item.opcode);
      if (numeric_entry_active) {
        original.stack.at(0) = "digits(" + old_original.at(0) + "," + digit + ")";
        rewritten.stack.at(0) = "digits(" + old_rewritten.at(0) + "," + digit + ")";
      } else {
        original.stack = {"digit." + digit, old_original.at(0), old_original.at(1),
                          old_original.at(2)};
        rewritten.stack = {"digit." + digit, old_rewritten.at(0), old_rewritten.at(1),
                           old_rewritten.at(2)};
      }
      // The entry command observes X2, which was proved equal above.  Its
      // exact normalized representation is irrelevant to the relational
      // proof, but retaining the common symbol prevents a false X2 kill.
      original.x2 = "number-entry(" + original.x2 + ")";
      rewritten.x2 = "number-entry(" + rewritten.x2 + ")";
      numeric_entry_active = true;
      continue;
    }
    numeric_entry_active = false;

    if (item.opcode == kDirectJumpOpcode) {
      if (item_index + 1U >= items.size() ||
          items.at(item_index + 1U).kind != MachineItemKind::Address ||
          items.at(item_index + 1U).formal_opcode.has_value() ||
          !std::holds_alternative<std::string>(items.at(item_index + 1U).target)) {
        proof.reason = "symbolic continuation reached an unresolved direct jump";
        return proof;
      }
      if (proof.cells >= options.max_continuation_cells) {
        proof.reason = "symbolic continuation exceeds the configured bound";
        return proof;
      }
      ++proof.cells;
      const ArtifactIndex index = index_artifact(items);
      const std::string& label =
          std::get<std::string>(items.at(item_index + 1U).target);
      const auto target = index.label_items.find(label);
      if (target == index.label_items.end() || index.duplicate_labels.contains(label)) {
        proof.reason = "symbolic continuation reached a missing or duplicate jump label";
        return proof;
      }
      if (target->second <= item_index + 1U) {
        proof.reason = "symbolic continuation does not follow backward or cyclic jumps";
        return proof;
      }
      HelperInvariantRecallHoistOptions tail_options = options;
      tail_options.max_continuation_cells -= proof.cells;
      ContinuationProof tail = prove_continuation(items, target->second, std::move(original),
                                                  std::move(rewritten), tail_options);
      proof.cells += tail.cells;
      proof.proved = tail.proved;
      proof.reason = std::move(tail.reason);
      return proof;
    }

    if (is_flow_opcode(item.opcode) || is_any_indirect_opcode(item.opcode)) {
      if ((item.opcode == kStopOpcode || item.opcode == kReturnOpcode) &&
          observable_values_equal(original, rewritten) &&
          original.stack.at(0) == rewritten.stack.at(0)) {
        // Both commands overwrite X2 from the same X before control leaves.
        proof.proved = true;
        return proof;
      }
      proof.reason = "control flow is reached before stack/X2 convergence";
      return proof;
    }

    std::string rejection;
    if (!execute_symbolic_pair(original, rewritten, item, false, false, rejection)) {
      proof.reason = rejection;
      return proof;
    }
  }
  if (states_equal(original, rewritten)) {
    proof.proved = true;
    return proof;
  }
  proof.reason = "symbolic continuation ends before stack/X2 convergence";
  return proof;
}

bool simulate_helper_body_pair(const std::vector<MachineItem>& items, std::size_t begin,
                               std::size_t return_index, SymbolicState& original,
                               SymbolicState& rewritten, std::string& rejection) {
  for (std::size_t item_index = begin; item_index < return_index; ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label)
      continue;
    if (!execute_symbolic_pair(original, rewritten, items.at(item_index), false, false,
                               rejection)) {
      return false;
    }
  }
  return execute_symbolic_pair(original, rewritten, items.at(return_index), true, false, rejection);
}

bool prove_call_transfer(const std::vector<MachineItem>& items,
                         const HelperInvariantRecallHoistProof& helper,
                         HelperInvariantRecallCall& call,
                         const HelperInvariantRecallHoistOptions& options, std::string& rejection) {
  SymbolicState original = initial_symbolic_state();
  SymbolicState rewritten = initial_symbolic_state();
  const MachineItem& recall = items.at(call.recall_item_index);

  if (call.placement == HelperInvariantRecallPlacement::BeforeCall) {
    if (!execute_symbolic_op(original, recall, false, rejection) ||
        !execute_symbolic_op(rewritten, recall, false, rejection)) {
      return false;
    }
    if (!simulate_helper_body_pair(items, helper.helper_body_begin_item_index,
                                   helper.helper_return_item_index, original, rewritten,
                                   rejection)) {
      return false;
    }
  } else {
    if (!execute_symbolic_op(rewritten, recall, false, rejection))
      return false;
    if (!simulate_helper_body_pair(items, helper.helper_body_begin_item_index,
                                   helper.helper_return_item_index, original, rewritten,
                                   rejection)) {
      return false;
    }
    if (!execute_symbolic_op(original, recall, false, rejection))
      return false;

    const MachineItem& join = items.at(call.continuation_item_index - 1U);
    if (!same_evaluated_operands(original, rewritten, join.opcode, true)) {
      rejection = "post-return K AND/K OR operands are not a proved commutative permutation";
      return false;
    }
    if (!execute_symbolic_op(original, join, false, rejection) ||
        !execute_symbolic_op(rewritten, join, false, rejection)) {
      return false;
    }
  }

  const ContinuationProof continuation =
      prove_continuation(items, call.continuation_item_index, original, rewritten, options);
  call.proved_continuation_cells = continuation.cells;
  if (!continuation.proved) {
    rejection = continuation.reason;
    return false;
  }
  return true;
}

void validate_pre_artifact_flow(const std::vector<MachineItem>& items, const ArtifactIndex& index,
                                const std::set<std::size_t>& removed_recall_items,
                                const HelperInvariantRecallHoistOptions& options,
                                HelperInvariantRecallHoistProof& proof) {
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Op && is_indirect_flow_opcode(item.opcode)) {
      const auto targets = options.proved_indirect_flow_targets.find(item_index);
      if (targets == options.proved_indirect_flow_targets.end()) {
        add_reason(proof, "unknown indirect flow prevents a complete helper-entry proof");
      } else {
        const int helper_start = index.item_addresses.at(proof.helper_label_item_index);
        const int helper_return = index.item_addresses.at(proof.helper_return_item_index);
        for (const int target : targets->second) {
          const auto target_item = index.cell_items.find(target);
          if (target_item == index.cell_items.end()) {
            add_reason(proof, "proved indirect target is outside the input artifact");
          } else if (items.at(target_item->second).kind != MachineItemKind::Op) {
            add_reason(proof, "proved indirect target is not a stable opcode cell");
          } else if (target >= helper_start && target <= helper_return) {
            add_reason(proof, "proved indirect target can enter the helper body");
          } else if (removed_recall_items.contains(target_item->second)) {
            add_reason(proof, "proved indirect target names a removed call-site recall");
          }
        }
      }
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    if (item.formal_opcode.has_value() || std::holds_alternative<int>(item.target)) {
      add_reason(proof, "fixed/formal address operand cannot be safely reindexed");
    }
  }

  for (const auto& [item_index, targets] : options.proved_indirect_flow_targets) {
    (void)targets;
    if (item_index >= items.size() || items.at(item_index).kind != MachineItemKind::Op ||
        !is_indirect_flow_opcode(items.at(item_index).opcode)) {
      add_reason(proof, "indirect-flow proof map contains a stale item index");
    }
  }
}

bool reindex_indirect_flow_proof(const std::vector<MachineItem>& original,
                                 const ArtifactIndex& original_index,
                                 const std::vector<MachineItem>& candidate,
                                 const ArtifactIndex& candidate_index,
                                 const std::vector<std::optional<std::size_t>>& old_to_new_item,
                                 const HelperInvariantRecallHoistOptions& options,
                                 HelperInvariantRecallHoistProof& proof) {
  proof.final_indirect_flow_targets.clear();
  for (const auto& [old_flow_item, old_targets] : options.proved_indirect_flow_targets) {
    if (old_flow_item >= original.size() || old_flow_item >= old_to_new_item.size() ||
        !old_to_new_item.at(old_flow_item).has_value()) {
      add_reason(proof, "proved indirect-flow command disappeared during rewrite");
      return false;
    }
    const std::size_t new_flow_item = *old_to_new_item.at(old_flow_item);
    if (new_flow_item >= candidate.size() ||
        candidate.at(new_flow_item).kind != MachineItemKind::Op ||
        !is_indirect_flow_opcode(candidate.at(new_flow_item).opcode)) {
      add_reason(proof, "proved indirect-flow command changed during rewrite");
      return false;
    }

    std::vector<int> new_targets;
    new_targets.reserve(old_targets.size());
    for (const int old_target : old_targets) {
      const auto old_target_item = original_index.cell_items.find(old_target);
      if (old_target_item == original_index.cell_items.end() ||
          old_target_item->second >= old_to_new_item.size() ||
          !old_to_new_item.at(old_target_item->second).has_value()) {
        add_reason(proof, "proved indirect target disappeared during rewrite");
        return false;
      }
      const std::size_t new_target_item = *old_to_new_item.at(old_target_item->second);
      if (new_target_item >= candidate_index.item_addresses.size()) {
        add_reason(proof, "proved indirect target could not be reindexed");
        return false;
      }
      new_targets.push_back(candidate_index.item_addresses.at(new_target_item));
    }
    proof.final_indirect_flow_targets.emplace(new_flow_item, std::move(new_targets));
  }
  return true;
}

bool verify_final_artifact(const std::vector<MachineItem>& original,
                           const std::vector<MachineItem>& candidate,
                           HelperInvariantRecallHoistProof& proof) {
  const ArtifactIndex index = index_artifact(candidate);
  const int expected_cells = proof.input_cells - static_cast<int>(proof.calls.size()) + 1;
  proof.output_cells = index.cells;
  if (index.cells != expected_cells) {
    add_reason(proof, "final artifact has an unexpected cell count");
    return false;
  }
  const auto root = index.label_items.find(proof.helper_label);
  if (root == index.label_items.end() || root->second + 1U >= candidate.size() ||
      candidate.at(root->second + 1U).kind != MachineItemKind::Op ||
      candidate.at(root->second + 1U).opcode != proof.recall_opcode) {
    add_reason(proof, "final artifact does not start the helper with the hoisted recall");
    return false;
  }
  std::optional<std::size_t> final_helper_return;
  for (std::size_t item_index = root->second + 1U; item_index < candidate.size(); ++item_index) {
    if (candidate.at(item_index).kind == MachineItemKind::Op &&
        candidate.at(item_index).opcode == kReturnOpcode) {
      final_helper_return = item_index;
      break;
    }
  }
  if (!final_helper_return.has_value()) {
    add_reason(proof, "final artifact lost the helper return");
    return false;
  }
  const int final_helper_start_address = index.item_addresses.at(root->second);
  const int final_helper_return_address = index.item_addresses.at(*final_helper_return);

  int original_recall_count = 0;
  int candidate_recall_count = 0;
  for (const MachineItem& item : original)
    if (item.kind == MachineItemKind::Op && item.opcode == proof.recall_opcode)
      ++original_recall_count;
  for (const MachineItem& item : candidate)
    if (item.kind == MachineItemKind::Op && item.opcode == proof.recall_opcode)
      ++candidate_recall_count;
  if (candidate_recall_count != original_recall_count - static_cast<int>(proof.calls.size()) + 1) {
    add_reason(proof, "final artifact did not remove exactly one recall per call site");
    return false;
  }

  int direct_root_calls = 0;
  for (std::size_t item_index = 0; item_index < candidate.size(); ++item_index) {
    const MachineItem& item = candidate.at(item_index);
    if (item.kind == MachineItemKind::Op && is_indirect_flow_opcode(item.opcode)) {
      const auto targets = proof.final_indirect_flow_targets.find(item_index);
      if (targets == proof.final_indirect_flow_targets.end()) {
        add_reason(proof, "final artifact contains unknown indirect flow");
        return false;
      }
      for (const int target : targets->second) {
        const auto target_item = index.cell_items.find(target);
        if (target_item == index.cell_items.end()) {
          add_reason(proof, "final proved indirect target is outside the artifact");
          return false;
        }
        if (candidate.at(target_item->second).kind != MachineItemKind::Op) {
          add_reason(proof, "final proved indirect target is not an opcode cell");
          return false;
        }
        if (target >= final_helper_start_address && target <= final_helper_return_address) {
          add_reason(proof, "final proved indirect target can enter the helper body");
          return false;
        }
      }
    }
    if (item.kind != MachineItemKind::Address)
      continue;
    if (item.formal_opcode.has_value() || std::holds_alternative<int>(item.target)) {
      add_reason(proof, "final artifact contains a fixed/formal address operand");
      return false;
    }
    const std::string& label = std::get<std::string>(item.target);
    if (!index.label_items.contains(label)) {
      add_reason(proof, "final artifact contains a missing label target");
      return false;
    }
    if (label != proof.helper_label)
      continue;
    if (item_index == 0 || candidate.at(item_index - 1U).kind != MachineItemKind::Op ||
        candidate.at(item_index - 1U).opcode != kDirectCallOpcode) {
      add_reason(proof, "final helper root has a non-PP reference");
      return false;
    }
    ++direct_root_calls;
  }
  if (direct_root_calls != static_cast<int>(proof.calls.size())) {
    add_reason(proof, "final helper direct-call set differs from the proved set");
    return false;
  }
  for (const auto& [item_index, targets] : proof.final_indirect_flow_targets) {
    (void)targets;
    if (item_index >= candidate.size() || candidate.at(item_index).kind != MachineItemKind::Op ||
        !is_indirect_flow_opcode(candidate.at(item_index).opcode)) {
      add_reason(proof, "final indirect-flow proof map contains a stale item index");
      return false;
    }
  }
  return true;
}

} // namespace

HelperInvariantRecallHoistProof
verify_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                     const std::string& helper_label,
                                     const HelperInvariantRecallHoistOptions& options) {
  HelperInvariantRecallHoistProof proof;
  proof.helper_label = helper_label;
  const ArtifactIndex index = index_artifact(items);
  proof.input_cells = index.cells;

  if (options.max_helper_body_cells == 0 || options.max_continuation_cells == 0)
    add_reason(proof, "symbolic proof bounds must both be positive");
  if (index.duplicate_labels.contains(helper_label))
    add_reason(proof, "helper root label is duplicated");
  const auto root = index.label_items.find(helper_label);
  if (root == index.label_items.end()) {
    add_reason(proof, "helper root label is absent");
    return proof;
  }
  proof.helper_label_item_index = root->second;
  proof.helper_body_begin_item_index = root->second + 1U;
  if (!has_fallthrough_barrier_before(items, root->second))
    add_reason(proof, "official control can fall through into the helper root");

  bool found_return = false;
  for (std::size_t item_index = proof.helper_body_begin_item_index; item_index < items.size();
       ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind == MachineItemKind::Label) {
      add_reason(proof, "helper contains a second executable entry label");
      continue;
    }
    if (item.kind != MachineItemKind::Op) {
      add_reason(proof, "helper contains an address cell before its return");
      break;
    }
    if (item.opcode == kReturnOpcode) {
      proof.helper_return_item_index = item_index;
      found_return = true;
      break;
    }
    ++proof.helper_body_cells;
    if (proof.helper_body_cells > options.max_helper_body_cells)
      add_reason(proof, "helper body exceeds the configured symbolic bound");
    if (item.raw || is_flow_opcode(item.opcode) || is_any_indirect_opcode(item.opcode) ||
        is_direct_register_alias(item.opcode))
      add_reason(proof, "helper is not a supported straight-line direct-register body");
    const OpcodeInfo& info = opcode_by_code(item.opcode);
    if (info.takes_address || info.stack_effect == StackEffect::Barrier ||
        info.stack_effect == StackEffect::Unknown || info.x2_effect == X2Effect::Restores ||
        info.x2_effect == X2Effect::Unknown) {
      add_reason(proof, "helper contains an unsupported stack/X2 command");
    }
  }
  if (!found_return)
    add_reason(proof, "helper has no explicit straight-line return");
  if (proof.helper_body_cells == 0)
    add_reason(proof, "helper body is empty");

  if (found_return) {
    const int helper_start_address = index.item_addresses.at(root->second);
    const int helper_return_address = index.item_addresses.at(proof.helper_return_item_index);
    for (const auto& [label, address] : index.label_addresses) {
      if (label != helper_label && address >= helper_start_address &&
          address <= helper_return_address) {
        const MachineItem& label_item = items.at(index.label_items.at(label));
        const bool preceding_end_metadata = address == helper_start_address &&
                                            label_item.procedure_boundary.has_value() &&
                                            *label_item.procedure_boundary == "end";
        if (!preceding_end_metadata)
          add_reason(proof, "helper contains a second executable entry label");
      }
    }
  }

  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    const MachineItem& item = items.at(item_index);
    if (item.kind != MachineItemKind::Address || !std::holds_alternative<std::string>(item.target))
      continue;
    const std::string& label = std::get<std::string>(item.target);
    const auto target = index.label_addresses.find(label);
    if (target == index.label_addresses.end()) {
      add_reason(proof, "address operand references a missing label");
      continue;
    }
    const bool targets_helper_range =
        found_return && target->second >= index.item_addresses.at(root->second) &&
        target->second <= index.item_addresses.at(proof.helper_return_item_index);
    if (!targets_helper_range)
      continue;
    if (label != helper_label) {
      add_reason(proof, "helper has a second referenced entry");
      continue;
    }
    if (item_index == 0 || items.at(item_index - 1U).kind != MachineItemKind::Op ||
        items.at(item_index - 1U).opcode != kDirectCallOpcode) {
      add_reason(proof, "helper root has a non-PP reference");
      continue;
    }
    proof.calls.push_back(HelperInvariantRecallCall{
        .call_item_index = item_index - 1U,
        .operand_item_index = item_index,
    });
  }
  if (proof.calls.size() < 2U)
    add_reason(proof, "helper needs at least two proved direct PP call sites");

  const std::vector<HelperInvariantRecallCall> unplanned_calls = proof.calls;
  std::set<int> candidate_recall_opcodes;
  for (const HelperInvariantRecallCall& call : unplanned_calls) {
    if (call.call_item_index > 0 &&
        items.at(call.call_item_index - 1U).kind == MachineItemKind::Op &&
        is_direct_recall(items.at(call.call_item_index - 1U).opcode)) {
      candidate_recall_opcodes.insert(items.at(call.call_item_index - 1U).opcode);
    }
    if (call.operand_item_index + 2U < items.size() &&
        items.at(call.operand_item_index + 1U).kind == MachineItemKind::Op &&
        is_direct_recall(items.at(call.operand_item_index + 1U).opcode) &&
        items.at(call.operand_item_index + 2U).kind == MachineItemKind::Op &&
        is_commutative_join(items.at(call.operand_item_index + 2U).opcode)) {
      candidate_recall_opcodes.insert(items.at(call.operand_item_index + 1U).opcode);
    }
  }

  int common_recall_opcode = -1;
  for (const int candidate_opcode : candidate_recall_opcodes) {
    std::vector<HelperInvariantRecallCall> planned_calls = unplanned_calls;
    bool complete = true;
    for (HelperInvariantRecallCall& call : planned_calls) {
      const bool matching_before =
          call.call_item_index > 0 &&
          items.at(call.call_item_index - 1U).kind == MachineItemKind::Op &&
          items.at(call.call_item_index - 1U).opcode == candidate_opcode;
      const bool matching_after =
          call.operand_item_index + 2U < items.size() &&
          items.at(call.operand_item_index + 1U).kind == MachineItemKind::Op &&
          items.at(call.operand_item_index + 1U).opcode == candidate_opcode &&
          items.at(call.operand_item_index + 2U).kind == MachineItemKind::Op &&
          is_commutative_join(items.at(call.operand_item_index + 2U).opcode);
      if (matching_before == matching_after) {
        complete = false;
        break;
      }
      if (matching_before) {
        call.placement = HelperInvariantRecallPlacement::BeforeCall;
        call.recall_item_index = call.call_item_index - 1U;
        call.continuation_item_index = call.operand_item_index + 1U;
      } else {
        call.placement = HelperInvariantRecallPlacement::AfterReturnBeforeCommutative;
        call.recall_item_index = call.operand_item_index + 1U;
        call.commutative_opcode = items.at(call.operand_item_index + 2U).opcode;
        call.continuation_item_index = call.operand_item_index + 3U;
      }
    }
    if (!complete)
      continue;
    common_recall_opcode = candidate_opcode;
    proof.calls = std::move(planned_calls);
    break;
  }
  if (common_recall_opcode < 0)
    add_reason(proof, "no single direct-register recall covers every helper call site");
  proof.recall_opcode = common_recall_opcode;
  if (common_recall_opcode >= 0)
    proof.register_index = common_recall_opcode - kFirstDirectRecallOpcode;

  if (found_return && proof.register_index >= 0) {
    for (std::size_t item_index = proof.helper_body_begin_item_index;
         item_index < proof.helper_return_item_index; ++item_index) {
      const MachineItem& item = items.at(item_index);
      if (item.kind == MachineItemKind::Op &&
          opcode_accesses_register(item.opcode, proof.register_index)) {
        add_reason(proof, "helper reads or writes the register selected for recall hoisting");
      }
    }
  }

  std::set<std::size_t> removed_recall_items;
  for (const HelperInvariantRecallCall& call : proof.calls) {
    if (call.recall_item_index < items.size() &&
        items.at(call.recall_item_index).kind == MachineItemKind::Op &&
        is_direct_recall(items.at(call.recall_item_index).opcode)) {
      removed_recall_items.insert(call.recall_item_index);
    }
  }
  if (found_return)
    validate_pre_artifact_flow(items, index, removed_recall_items, options, proof);

  if (proof.reasons.empty()) {
    for (HelperInvariantRecallCall& call : proof.calls) {
      std::string rejection;
      if (!prove_call_transfer(items, proof, call, options, rejection)) {
        add_reason(proof, "call-site " +
                              std::to_string(index.item_addresses.at(call.call_item_index)) +
                              " symbolic proof failed: " + rejection);
      }
    }
  }

  proof.output_cells = proof.input_cells - static_cast<int>(proof.calls.size()) + 1;
  if (proof.output_cells >= proof.input_cells)
    add_reason(proof, "recall hoist is not cell-profitable");
  proof.proved = proof.reasons.empty();
  return proof;
}

HelperInvariantRecallHoistResult
rewrite_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                      const std::string& helper_label,
                                      const HelperInvariantRecallHoistOptions& options) {
  HelperInvariantRecallHoistResult result;
  result.items = items;
  result.proof = verify_helper_invariant_recall_hoist(items, helper_label, options);
  if (!result.proof.proved)
    return result;

  std::set<std::size_t> removed_recalls;
  for (const HelperInvariantRecallCall& call : result.proof.calls)
    removed_recalls.insert(call.recall_item_index);
  MachineItem hoisted = items.at(result.proof.calls.front().recall_item_index);
  hoisted.roles.push_back("helper-invariant-recall-hoist:root");

  std::vector<MachineItem> candidate;
  candidate.reserve(items.size() - removed_recalls.size() + 1U);
  std::vector<std::optional<std::size_t>> old_to_new_item(items.size());
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (removed_recalls.contains(item_index))
      continue;
    old_to_new_item.at(item_index) = candidate.size();
    candidate.push_back(items.at(item_index));
    if (item_index == result.proof.helper_label_item_index)
      candidate.push_back(hoisted);
  }

  const ArtifactIndex original_index = index_artifact(items);
  const ArtifactIndex candidate_index = index_artifact(candidate);
  if (!reindex_indirect_flow_proof(items, original_index, candidate, candidate_index,
                                   old_to_new_item, options, result.proof)) {
    result.proof.proved = false;
    result.items = items;
    return result;
  }
  result.proof.final_artifact_proved = verify_final_artifact(items, candidate, result.proof);
  if (!result.proof.final_artifact_proved) {
    result.proof.proved = false;
    result.items = items;
    return result;
  }

  result.items = std::move(candidate);
  result.applied = 1;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "helper-invariant-recall-hoist",
      .detail = "Hoisted one invariant direct-register recall into straight-line helper " +
                helper_label + " after bounded X/Y/Z/T/X2 proofs at " +
                std::to_string(result.proof.calls.size()) + " complete direct call sites; saved " +
                std::to_string(result.proof.input_cells - result.proof.output_cells) + " cells.",
  });
  return result;
}

HelperInvariantRecallHoistResult
optimize_helper_invariant_recall_hoist(const std::vector<MachineItem>& items,
                                       const HelperInvariantRecallHoistOptions& options) {
  std::vector<std::string> labels;
  std::set<std::string> seen;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label && seen.insert(item.name).second)
      labels.push_back(item.name);
  }

  HelperInvariantRecallHoistResult rejection;
  rejection.items = items;
  for (const std::string& label : labels) {
    HelperInvariantRecallHoistResult candidate =
        rewrite_helper_invariant_recall_hoist(items, label, options);
    if (candidate.applied > 0)
      return candidate;
    if (!candidate.proof.reasons.empty() &&
        (candidate.proof.calls.size() > rejection.proof.calls.size() ||
         (candidate.proof.calls.size() == rejection.proof.calls.size() &&
          (rejection.proof.reasons.empty() ||
           candidate.proof.reasons.size() < rejection.proof.reasons.size())))) {
      rejection.proof = std::move(candidate.proof);
    }
  }
  return rejection;
}

} // namespace mkpro::core
