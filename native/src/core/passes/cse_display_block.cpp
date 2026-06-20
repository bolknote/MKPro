#include "mkpro/core/passes/cse_display_block.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct Block {
  int start_index = 0;
  std::vector<IrOp> ops;
};

struct Replacement {
  int count = 0;
  std::string label;
  IrTargetMeta target_meta;
  IrMeta jump_meta;
};

bool is_pure_data_op(const IrOp& op) {
  if (has_rewrite_barrier(op))
    return false;
  if (op.kind == IrKind::Recall)
    return true;
  if (op.kind == IrKind::Plain) {
    if (op.opcode == 0x10 || op.opcode == 0x12)
      return true;
    if (op.opcode <= 0x09 || op.opcode == 0x0a)
      return true;
  }
  return false;
}

std::vector<Block> collect_cse_candidates(const std::vector<IrOp>& ops) {
  std::vector<Block> blocks;
  int start = -1;
  std::vector<IrOp> buffer;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label) {
      start = -1;
      buffer.clear();
      continue;
    }
    if (is_pure_data_op(op)) {
      if (start == -1)
        start = static_cast<int>(index);
      buffer.push_back(op);
      continue;
    }
    if (buffer.size() >= 3U && op.kind == IrKind::Return) {
      std::vector<IrOp> block_ops = buffer;
      block_ops.push_back(op);
      blocks.push_back(Block{.start_index = start, .ops = std::move(block_ops)});
    } else if (buffer.size() >= 3U && op.kind == IrKind::Stop &&
               index + 1U < ops.size() && ops.at(index + 1U).kind == IrKind::Return) {
      std::vector<IrOp> block_ops = buffer;
      block_ops.push_back(op);
      block_ops.push_back(ops.at(index + 1U));
      blocks.push_back(Block{.start_index = start, .ops = std::move(block_ops)});
    }
    start = -1;
    buffer.clear();
  }

  return blocks;
}

std::string block_signature(const Block& block) {
  std::string signature;
  bool first = true;
  auto append = [&](const std::string& value) {
    if (!first)
      signature += "|";
    signature += value;
    first = false;
  };

  for (const IrOp& op : block.ops) {
    if (op.kind == IrKind::Recall) {
      append("r:" + op.register_name);
    } else if (op.kind == IrKind::Plain) {
      append("p:" + std::to_string(op.opcode));
    } else if (op.kind == IrKind::Stop) {
      append("stop:" + op.semantic);
    } else if (op.kind == IrKind::Return) {
      append("return");
    } else {
      append("o:" + ir_kind_name(op.kind));
    }
  }
  return signature;
}

std::string fresh_label() {
  static int cse_label_counter = 0;
  ++cse_label_counter;
  return "__cse_block_" + std::to_string(cse_label_counter);
}

} // namespace

PassResult cse_display_block(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  if (ops.empty())
    return PassResult{.ops = {}, .applied = 0, .optimizations = {}};

  const std::vector<Block> blocks = collect_cse_candidates(ops);
  if (blocks.size() < 2U)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<std::string, std::vector<Block>> by_signature;
  for (const Block& block : blocks)
    by_signature[block_signature(block)].push_back(block);

  int applied = 0;
  std::map<int, Replacement> replace_with;
  std::map<int, std::string> labels_to_insert;

  for (const auto& [signature, group] : by_signature) {
    (void)signature;
    if (group.size() < 2U)
      continue;
    const int block_size = static_cast<int>(group.at(0).ops.size());
    const int saved_per_site = block_size - 2;
    if (saved_per_site < 1)
      continue;
    const int total_savings = saved_per_site * (static_cast<int>(group.size()) - 1);
    if (total_savings <= 0)
      continue;

    const Block& canonical = group.at(0);
    const std::string label = fresh_label();
    labels_to_insert[canonical.start_index] = label;
    for (std::size_t index = 1; index < group.size(); ++index) {
      const Block& duplicate = group.at(index);
      replace_with[duplicate.start_index] = Replacement{
          .count = static_cast<int>(duplicate.ops.size()),
          .label = label,
          .target_meta = IrTargetMeta{.comment = "cse jump"},
          .jump_meta = IrMeta{.mnemonic = "БП", .comment = "cse"},
      };
      ++applied;
    }
  }

  if (applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::vector<IrOp> result;
  int index = 0;
  while (index < static_cast<int>(ops.size())) {
    const auto label = labels_to_insert.find(index);
    if (label != labels_to_insert.end()) {
      IrOp label_op;
      label_op.kind = IrKind::Label;
      label_op.name = label->second;
      result.push_back(std::move(label_op));
    }

    const auto replacement = replace_with.find(index);
    if (replacement != replace_with.end()) {
      IrOp jump;
      jump.kind = IrKind::Jump;
      jump.target = replacement->second.label;
      jump.opcode = 0x51;
      jump.meta = replacement->second.jump_meta;
      jump.target_meta = replacement->second.target_meta;
      result.push_back(std::move(jump));
      index += replacement->second.count;
      continue;
    }

    result.push_back(ops.at(static_cast<std::size_t>(index)));
    ++index;
  }

  return PassResult{
      .ops = std::move(result),
      .applied = applied,
      .optimizations =
          {
              AppliedOptimization{
                  .name = "cse-display-block",
                  .detail = "Deduplicated " + std::to_string(applied) +
                            " display block(s) by redirecting to a shared exit.",
              },
          },
  };
}

IrPass cse_display_block_pass() {
  return IrPass{
      .name = "cse-display-block",
      .run = cse_display_block,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
