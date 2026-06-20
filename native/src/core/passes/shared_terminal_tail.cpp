#include "mkpro/core/passes/shared_terminal_tail.hpp"

#include "mkpro/core/passes/outline.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

bool is_shareable_terminal(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  return op.kind == IrKind::Jump || op.kind == IrKind::IndirectJump || op.kind == IrKind::Return;
}

bool is_shareable_body_op(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  switch (op.kind) {
  case IrKind::Store:
  case IrKind::Recall:
  case IrKind::IndirectStore:
  case IrKind::IndirectRecall:
  case IrKind::Call:
  case IrKind::IndirectCall:
  case IrKind::Stop:
  case IrKind::Plain:
    return true;
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Loop:
  case IrKind::IndirectJump:
  case IrKind::IndirectCondJump:
  case IrKind::Return:
  case IrKind::OrphanAddress:
    return false;
  }
  return false;
}

std::string op_key(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return "store:" + op.register_name;
  case IrKind::Recall:
    return "recall:" + op.register_name;
  case IrKind::IndirectStore:
    return "indirect-store:" + op.register_name;
  case IrKind::IndirectRecall:
    return "indirect-recall:" + op.register_name;
  case IrKind::IndirectJump:
    return "indirect-jump:" + op.register_name;
  case IrKind::IndirectCall:
    return "indirect-call:" + op.register_name;
  case IrKind::Stop:
    return "stop:" + op.semantic;
  case IrKind::Jump:
    return "jump:" + target_key(op.target);
  case IrKind::Call:
    return "call:" + target_key(op.target);
  case IrKind::Return:
    return "return";
  case IrKind::Plain:
    return "plain:" + std::to_string(op.opcode);
  case IrKind::Label:
    return "label:" + op.name;
  case IrKind::CondJump:
    return "cjump:" + op.condition + ":" + target_key(op.target);
  case IrKind::Loop:
    return "loop:" + op.counter + ":" + target_key(op.target);
  case IrKind::IndirectCondJump:
    return "indirect-cjump:" + op.condition + ":" + op.register_name;
  case IrKind::OrphanAddress:
    return "address:" + target_key(op.target);
  }
  return {};
}

IrOp shared_tail_jump(const std::string& label, const IrOp& source) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.target = label;
  op.opcode = 0x51;
  op.meta.mnemonic = "БП";
  op.meta.comment = "shared terminal tail";
  op.meta.source_line = source.meta.source_line;
  op.target_meta.comment = "shared terminal tail";
  return op;
}

} // namespace

PassResult shared_terminal_tail(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  if (has_numeric_outline_flow_target(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<OutlineCandidate> candidates = collect_suffix_candidates(
      ops, SuffixCollectionConfig{
               .is_terminal = is_shareable_terminal,
               .is_body_op = is_shareable_body_op,
               .op_key = op_key,
           });
  LabelAllocator labels(ops, "__shared_terminal_tail_");
  std::set<int> protected_indexes;
  const std::vector<SelectedOutline> selected =
      select_shared_suffixes(candidates, labels, protected_indexes);
  if (selected.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<int, std::vector<std::string>> target_labels;
  struct Replacement {
    int end = 0;
    std::string label;
  };
  std::map<int, Replacement> replacement_by_start;
  int applied = 0;
  int saved_cells = 0;

  for (const SelectedOutline& tail : selected) {
    target_labels[tail.target.start].push_back(tail.label);
    for (const OutlineOccurrence& replacement : tail.replacements) {
      replacement_by_start[replacement.start] =
          Replacement{.end = replacement.end, .label = tail.label};
      ++applied;
      saved_cells += replacement.cells - 2;
    }
  }

  std::vector<IrOp> result;
  result.reserve(ops.size() + target_labels.size());
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const auto labels_at_index = target_labels.find(index);
    if (labels_at_index != target_labels.end()) {
      for (const std::string& label : labels_at_index->second) {
        IrOp label_op;
        label_op.kind = IrKind::Label;
        label_op.name = label;
        result.push_back(std::move(label_op));
      }
    }

    const auto replacement = replacement_by_start.find(index);
    if (replacement != replacement_by_start.end()) {
      result.push_back(shared_tail_jump(replacement->second.label,
                                        ops.at(static_cast<std::size_t>(index))));
      index = replacement->second.end;
      continue;
    }

    result.push_back(ops.at(static_cast<std::size_t>(index)));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "shared-terminal-tail",
                  .detail = "Shared " + std::to_string(applied) +
                            " terminal straight-line tail" + (applied == 1 ? "" : "s") + " (" +
                            std::to_string(saved_cells) + " cell" +
                            (saved_cells == 1 ? "" : "s") + " saved).",
              },
          },
  };
}

IrPass shared_terminal_tail_pass() {
  return IrPass{
      .name = "shared-terminal-tail",
      .run = shared_terminal_tail,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
