#include "mkpro/core/passes/dead_proc_elimination.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct ProcBlock {
  std::string name;
  int start = 0;
  int end = 0;
};

std::vector<ProcBlock> collect_procedure_blocks(const std::vector<IrOp>& ops) {
  std::vector<ProcBlock> blocks;
  std::optional<ProcBlock> active;

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind != IrKind::Label)
      continue;
    if (op.procedure_boundary == "start") {
      active = ProcBlock{
          .name = op.procedure_name.value_or(op.name),
          .start = static_cast<int>(index),
          .end = static_cast<int>(index),
      };
      continue;
    }
    if (op.procedure_boundary == "end" && active.has_value()) {
      const std::string name = op.procedure_name.value_or(active->name);
      if (name == active->name) {
        blocks.push_back(ProcBlock{
            .name = name,
            .start = active->start,
            .end = static_cast<int>(index + 1U),
        });
      }
      active = std::nullopt;
    }
  }

  return blocks;
}

bool can_fall_through_past_block(const std::vector<IrOp>& ops, const ProcBlock& block) {
  for (int index = block.end - 1; index >= block.start; --index) {
    const IrOp& op = ops.at(static_cast<std::size_t>(index));
    if (op.kind == IrKind::Label)
      continue;
    if (op.kind == IrKind::Jump || op.kind == IrKind::Return ||
        op.kind == IrKind::IndirectJump) {
      return false;
    }
    if (op.kind == IrKind::Stop && op.semantic == "halt")
      return false;
    if (op.kind == IrKind::Plain && op.meta.comment.has_value() &&
        op.meta.comment->rfind("halt", 0) == 0) {
      return false;
    }
    return true;
  }
  return true;
}

bool has_raw_executable(const std::vector<IrOp>& ops) {
  for (const IrOp& op : ops) {
    if (op.kind != IrKind::Label && op.kind != IrKind::OrphanAddress && op.meta.raw)
      return true;
  }
  return false;
}

void add_edge(std::map<std::string, std::set<std::string>>& edges,
              const std::string& source_owner, const std::string& target_owner) {
  edges[source_owner].insert(target_owner);
}

} // namespace

PassResult dead_proc_elimination(const std::vector<IrOp>& ops, const PassContext& context) {
  if (context.options.disable_interprocedural_opts)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  if (has_raw_executable(ops))
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::vector<ProcBlock> blocks = collect_procedure_blocks(ops);
  if (blocks.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<int, std::string> owner_by_index;
  std::map<std::string, std::string> label_owner;
  for (const ProcBlock& block : blocks) {
    for (int index = block.start; index < block.end; ++index) {
      owner_by_index[index] = block.name;
      const IrOp& op = ops.at(static_cast<std::size_t>(index));
      if (op.kind == IrKind::Label)
        label_owner[op.name] = block.name;
    }
  }

  std::map<int, std::string> address_owner;
  int address = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    if (op.kind == IrKind::Label)
      continue;
    const auto owner = owner_by_index.find(static_cast<int>(index));
    if (owner != owner_by_index.end()) {
      for (int offset = 0; offset < cells_per_op(op); ++offset)
        address_owner[address + offset] = owner->second;
    }
    address += cells_per_op(op);
  }

  std::set<std::string> root_targets;
  std::map<std::string, std::set<std::string>> edges;
  auto mark_reference = [&](int index, const std::optional<std::string>& target_owner) {
    if (!target_owner.has_value())
      return;
    const auto source_owner = owner_by_index.find(index);
    if (source_owner == owner_by_index.end()) {
      root_targets.insert(*target_owner);
      return;
    }
    add_edge(edges, source_owner->second, *target_owner);
  };

  for (const ProcBlock& block : blocks) {
    if (!can_fall_through_past_block(ops, block))
      continue;
    const auto target_owner = owner_by_index.find(block.end);
    if (target_owner != owner_by_index.end() && target_owner->second != block.name)
      add_edge(edges, block.name, target_owner->second);
  }

  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    const bool is_flow =
        op.kind == IrKind::Jump || op.kind == IrKind::CondJump || op.kind == IrKind::Call ||
        op.kind == IrKind::Loop || op.kind == IrKind::OrphanAddress ||
        op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
        op.kind == IrKind::IndirectCondJump;
    if (!is_flow)
      continue;

    if (op.kind == IrKind::IndirectJump || op.kind == IrKind::IndirectCall ||
        op.kind == IrKind::IndirectCondJump) {
      const std::optional<int> target = known_indirect_flow_target(op);
      if (!target.has_value())
        return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
      const auto owner = address_owner.find(*target);
      mark_reference(static_cast<int>(index),
                     owner == address_owner.end() ? std::optional<std::string>{}
                                                  : std::optional<std::string>{owner->second});
      continue;
    }

    if (const auto* label = std::get_if<std::string>(&op.target)) {
      const auto owner = label_owner.find(*label);
      mark_reference(static_cast<int>(index),
                     owner == label_owner.end() ? std::optional<std::string>{}
                                                : std::optional<std::string>{owner->second});
    } else if (const auto* target_address = std::get_if<int>(&op.target)) {
      const auto owner = address_owner.find(*target_address);
      mark_reference(static_cast<int>(index),
                     owner == address_owner.end() ? std::optional<std::string>{}
                                                  : std::optional<std::string>{owner->second});
    }
  }

  std::set<std::string> reachable;
  std::vector<std::string> stack(root_targets.begin(), root_targets.end());
  while (!stack.empty()) {
    const std::string name = stack.back();
    stack.pop_back();
    if (reachable.contains(name))
      continue;
    reachable.insert(name);
    const auto outgoing = edges.find(name);
    if (outgoing == edges.end())
      continue;
    for (const std::string& target : outgoing->second)
      stack.push_back(target);
  }

  std::vector<ProcBlock> dead;
  for (const ProcBlock& block : blocks) {
    if (!reachable.contains(block.name))
      dead.push_back(block);
  }
  if (dead.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::set<int> remove;
  for (const ProcBlock& block : dead) {
    for (int index = block.start; index < block.end; ++index)
      remove.insert(index);
  }

  std::vector<IrOp> result;
  result.reserve(ops.size() - remove.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (!remove.contains(static_cast<int>(index)))
      result.push_back(ops.at(index));
  }

  return PassResult{
      .ops = std::move(result),
      .applied = static_cast<int>(remove.size()),
      .optimizations =
          {
              AppliedOptimization{
                  .name = "dead-proc-elimination",
                  .detail = "Removed " + std::to_string(dead.size()) +
                            " unreferenced emitted rule proc(s) after IR optimization.",
              },
          },
  };
}

IrPass dead_proc_elimination_pass() {
  return IrPass{
      .name = "dead-proc-elimination",
      .run = dead_proc_elimination,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
