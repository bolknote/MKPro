#include "mkpro/core/passes/semantic_region_folding.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace mkpro::core::passes {
namespace {

struct Component {
  std::size_t begin = 0;
  std::size_t end = 0;
  int cells = 0;
};

bool ends_chain(const IrOp& op) {
  return op.kind == IrKind::Return || op.kind == IrKind::Jump ||
         op.kind == IrKind::IndirectJump ||
         (op.kind == IrKind::Stop && op.meta.stop_disposition == StopDisposition::Terminal);
}

bool symbolic(const std::vector<IrOp>& ops, bool preserve_compiler_selector_charges = false) {
  if (ops.empty() || ops.size() > 640U)
    return false;
  for (const auto& op : ops) {
    if (op.meta.raw || op.meta.logical_register_analysis ||
        op.kind == IrKind::OrphanAddress || op.target_meta.formal_opcode ||
        op.meta.indirect_flow_formal_targets)
      return false;
    if ((op.kind == IrKind::Call || op.kind == IrKind::Jump ||
         op.kind == IrKind::CondJump || op.kind == IrKind::Loop) &&
        !std::holds_alternative<std::string>(op.target))
      return false;
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets)
        if (!std::holds_alternative<std::string>(target))
          return false;
    for (const auto& role : op.meta.roles)
      if (role == kResumableErrorPaddingRole || role == "exec" ||
          (!preserve_compiler_selector_charges && role.starts_with("late-decimal-selector-")) ||
          role.starts_with("finalization-cell-origin:"))
        return false;
  }
  return true;
}

std::vector<Component> components(const std::vector<IrOp>& ops) {
  std::vector<Component> result;
  Component current;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    current.cells += cells_per_op(ops[i]);
    if (ends_chain(ops[i])) {
      current.end = i + 1U;
      result.push_back(current);
      current = Component{i + 1U, i + 1U, 0};
    }
  }
  if (current.begin != ops.size()) {
    current.end = ops.size();
    result.push_back(current);
  }
  return result;
}

bool observation(const IrOp& op) {
  return op.kind == IrKind::Stop || op.meta.manual_interaction.has_value();
}

PostLayoutControlFlowOptions flow_options(const PassContext& context) {
  PostLayoutControlFlowOptions result;
  result.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  return result;
}

std::vector<std::size_t> machine_indexes(const std::vector<IrOp>& ops) {
  std::vector<std::size_t> result;
  std::size_t index = 0;
  for (const auto& op : ops) {
    result.push_back(index);
    index += op.kind == IrKind::Label ? 1U : static_cast<std::size_t>(cells_per_op(op));
  }
  return result;
}

bool relocated(const std::vector<IrOp>& before, const std::vector<IrOp>& after,
               const std::vector<std::size_t>& mapping, const PassContext& context,
               const PostLayoutControlFlowOptions* control_options = nullptr) {
  const auto input = lower_ir_to_machine(before);
  const auto output = lower_ir_to_machine(after);
  const auto old_machine = machine_indexes(before);
  const auto new_machine = machine_indexes(after);
  std::vector<std::optional<std::size_t>> items(input.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (mapping[i] >= after.size())
      return false;
    const std::size_t count = before[i].kind == IrKind::Label
        ? 1U : static_cast<std::size_t>(cells_per_op(before[i]));
    for (std::size_t cell = 0; cell < count; ++cell)
      items[old_machine[i] + cell] = new_machine[mapping[i]] + cell;
  }
  const auto options = control_options ? *control_options : flow_options(context);
  const auto old_flow = build_post_layout_control_flow(input, options);
  const auto new_flow = build_post_layout_control_flow(output, options);
  return old_flow.proved && new_flow.proved &&
         prove_post_layout_execution_relocation(input, output, old_flow, new_flow, items).proved;
}

auto effect_fields(const IrOp& op) {
  return std::tie(op.kind, op.register_name, op.condition, op.counter, op.opcode,
                  op.semantic, op.meta.raw, op.meta.roles, op.meta.stop_disposition,
                  op.meta.indirect_flow_formal_targets, op.meta.indirect_memory_targets,
                  op.meta.logical_register_name, op.meta.logical_indirect_memory_targets,
                  op.meta.logical_register_analysis, op.meta.discarded_indirect_recall_value,
                  op.meta.borrowed_entry_phase_selector, op.meta.tactic,
                  op.target_meta.roles, op.target_meta.formal_opcode);
}

std::map<std::string, int> positions(const std::vector<IrOp>& ops) {
  std::map<std::string, int> result;
  int address = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label)
      result.emplace(op.name, address);
    address += cells_per_op(op);
  }
  return result;
}

bool fixed_indirect_targets(const std::vector<IrOp>& before, const std::vector<IrOp>& after) {
  const auto old_positions = positions(before);
  const auto new_positions = positions(after);
  for (const auto& op : before)
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets) {
        const auto* label = std::get_if<std::string>(&target);
        if (!label || !old_positions.contains(*label) || !new_positions.contains(*label) ||
            old_positions.at(*label) != new_positions.at(*label))
          return false;
      }
  return true;
}

} // namespace

std::optional<SymbolicRegionLayout> layout_symbolic_regions(
    const std::vector<IrOp>& ops, const std::map<std::string, int>& required_targets,
    const PassContext& context, std::size_t maximum_work,
    const PostLayoutControlFlowOptions* control_options,
    bool preserve_compiler_selector_charges,
    const SymbolicRegionProjection* projection) {
  if (!symbolic(ops, preserve_compiler_selector_charges))
    return std::nullopt;
  auto parts = components(ops);
  if (parts.empty() || parts.size() > 24U)
    return std::nullopt;
  std::vector<int> widths;
  for (const auto& op : ops) widths.push_back(cells_per_op(op));
  if (projection) {
    if (projection->cell_widths.size() != ops.size())
      return std::nullopt;
    for (std::size_t i = 0; i < ops.size(); ++i) {
      const int width = projection->cell_widths[i];
      if (width < 0 || width > 8 || (ops[i].kind == IrKind::Label && width != 0))
        return std::nullopt;
    }
    widths = projection->cell_widths;
    for (auto& part : parts) {
      part.cells = 0;
      for (std::size_t i = part.begin; i < part.end; ++i) part.cells += widths[i];
    }
  }
  std::map<std::string, std::pair<std::size_t, int>> labels;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    int offset = 0;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      if (ops[i].kind == IrKind::Label && !labels.emplace(ops[i].name, std::pair{part, offset}).second)
        return std::nullopt;
      offset += widths[i];
    }
  }
  std::vector<std::optional<int>> starts(parts.size());
  starts[0] = 0; // The externally entered root is never moved.
  // A manual protocol is a local layout anchor, not a ban on optimizing
  // unrelated procedures. Keep its entire physical fallthrough component in
  // place; the matched-context transport proof still checks every external
  // entry and continuation after all other components have been placed.
  int original_address = 0;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      if (ops[i].meta.manual_interaction)
        starts[part] = original_address;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      original_address += cells_per_op(ops[i]);
  }
  std::vector<std::pair<int, int>> bounds(parts.size(), {0, 1000000});
  if (projection) {
    for (const auto& [label, bound] : projection->target_bounds) {
      const auto found = labels.find(label);
      if (found == labels.end() || bound.first < 0 || bound.first > bound.second)
        return std::nullopt;
      const auto [part, offset] = found->second;
      bounds[part].first = std::max(bounds[part].first, bound.first - offset);
      bounds[part].second = std::min(bounds[part].second, bound.second - offset);
      if (bounds[part].first > bounds[part].second)
        return std::nullopt;
    }
  }
  for (const auto& [label, address] : required_targets) {
    const auto found = labels.find(label);
    if (found == labels.end())
      return std::nullopt;
    const auto [part, offset] = found->second;
    const int start = address - offset;
    if (start < 0 || (starts[part] && *starts[part] != start))
      return std::nullopt;
    starts[part] = start;
  }
  std::size_t work = 0;
  std::vector<std::size_t> order;
  std::vector<bool> used(parts.size(), false);
  std::function<bool(int)> place = [&](int address) {
    if (work++ >= maximum_work)
      return false;
    if (order.size() == parts.size())
      return true;
    std::optional<std::size_t> due;
    int deadline = 1000000;
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (used[i] || !starts[i])
        continue;
      if (*starts[i] < address)
        return false;
      if (*starts[i] == address) {
        if (due && parts[i].cells != 0 && parts[*due].cells != 0)
          return false;
        due = i;
      } else {
        deadline = std::min(deadline, *starts[i]);
      }
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (used[i] || (due && i != *due) || (starts[i] && *starts[i] != address) ||
          address < bounds[i].first || address > bounds[i].second ||
          address + parts[i].cells > deadline)
        continue;
      used[i] = true;
      order.push_back(i);
      if (place(address + parts[i].cells))
        return true;
      order.pop_back();
      used[i] = false;
      if (work >= maximum_work)
        return false;
    }
    return false;
  };
  if (!place(0))
    return std::nullopt;
  SymbolicRegionLayout result;
  result.work = work;
  std::vector<std::size_t> mapping(ops.size());
  for (const auto part : order)
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      mapping[i] = result.ops.size();
      result.changed = result.changed || mapping[i] != i;
      if (ops[i].kind == IrKind::Label)
        result.projected_targets.emplace(ops[i].name, result.projected_cells);
      result.projected_cells += widths[i];
      result.ops.push_back(ops[i]);
    }
  if (!relocated(ops, result.ops, mapping, context, control_options))
    return std::nullopt;
  return result;
}



std::vector<PassResult> deferred_stack_producer_erasure_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control_options,
    const std::map<std::string, int>& encoded_target_origins,
    const DeferredStackProducerLimits& limits) {
  std::vector<PassResult> result;
  if (!symbolic(ops, true) || limits.maximum_candidates == 0 ||
      limits.maximum_erased_cells == 0 || limits.maximum_proof_attempts == 0)
    return result;
  struct Node {
    std::vector<MachineItem> items;
    std::size_t erased = 0;
  };
  std::vector<Node> nodes{{lower_ir_to_machine(ops), 0}};
  std::size_t attempts = 0;
  for (std::size_t parent = 0; parent < nodes.size(); ++parent) {
    // Appending children can reallocate nodes; keep this proof's input stable.
    const auto input = nodes[parent].items;
    const auto erased = nodes[parent].erased;
    if (erased >= limits.maximum_erased_cells) continue;
    const auto flow = build_post_layout_control_flow(input, control_options);
    if (!flow.proved) continue;
    auto data_flow = flow;
    const auto product = build_deferred_selector_consistent_execution_graph(
        input, flow, encoded_target_origins, control_options.maximum_execution_states);
    if (product.execution.proved) {
      data_flow.execution_states.clear();
      for (const auto state : product.execution.original_states)
        data_flow.execution_states.push_back(flow.execution_states[state]);
      data_flow.execution_successors = product.execution.successors;
      data_flow.execution_edges = product.execution.execution_edges;
    }
    // A failed refinement leaves the full conservative graph, never a partial
    // product. The data proof must not re-prune using stale physical digits.
    for (std::size_t at = 0; at < input.size(); ++at) {
      const auto& producer = input[at];
      const bool enter = producer.opcode == 0x0e;
      if (producer.kind != MachineItemKind::Op || producer.raw ||
          producer.manual_interaction || producer.procedure_boundary ||
          !producer.roles.empty() || !producer.semantic_call_origins.empty() ||
          (!enter && (producer.opcode < 0x60 || producer.opcode > 0x6e)))
        continue;
      std::size_t retained = at + 1U;
      while (retained < input.size() && input[retained].kind == MachineItemKind::Label)
        ++retained;
      if (retained >= input.size()) continue;
      const auto& closer = input[retained];
      if (closer.kind != MachineItemKind::Op ||
          !((closer.opcode >= 0x60 && closer.opcode <= 0x6e) ||
            (enter && closer.opcode >= 0x40 && closer.opcode <= 0x4e)))
        continue;
      if (attempts++ >= limits.maximum_proof_attempts) return result;
      if (!prove_post_layout_stack_producer_erasure(
              input, data_flow, at, {.refine_runtime_selectors = false}))
        continue;
      auto next = input;
      next.erase(next.begin() + static_cast<std::ptrdiff_t>(at));
      const auto next_flow = build_post_layout_control_flow(next, control_options);
      std::vector<std::optional<std::size_t>> relocation(input.size());
      for (std::size_t i = 0; i < input.size(); ++i)
        relocation[i] = i < at ? i : i - 1U;
      // A return to the erased cell now resumes at its retained successor.
      // The labelled CFG walk proves that alias in every caller context.
      relocation[at] = retained - 1U;
      if (!prove_post_layout_execution_relocation(
              input, next, flow, next_flow, relocation,
              {.erased_stack_producer_item = at}).proved)
        continue;
      if (std::any_of(nodes.begin(), nodes.end(), [&](const Node& node) {
            return node.items.size() == next.size() &&
                std::equal(node.items.begin(), node.items.end(), next.begin(), machine_items_equal);
          }))
        continue;
      PassResult candidate;
      candidate.ops = raise_machine_to_ir(
          next, effective_optimizer_feature_profile(context.options));
      candidate.applied = static_cast<int>(erased + 1U);
      candidate.optimizations.push_back({"joint-dead-stack-layout",
          "Retained " + std::to_string(erased + 1U) +
          " separately data/CFG-certified stack producer erasures before selector, "
          "region and shared-tail placement; final encoded targets remain obligations."});
      result.push_back(std::move(candidate));
      nodes.push_back({std::move(next), erased + 1U});
      if (result.size() >= limits.maximum_candidates) return result;
    }
  }
  return result;
}

std::vector<PassResult> conditional_region_exchange_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control_options,
    std::size_t maximum_candidates) {
  std::vector<PassResult> result;
  if (maximum_candidates == 0 || !symbolic(ops, true) ||
      control_options.address_space_model != flow_options(context).address_space_model)
    return result;
  const auto parts = components(ops);
  if (parts.size() < 2U || parts.size() > 24U) return result;
  std::map<std::string, std::size_t> labels;
  for (std::size_t i = 0; i < ops.size(); ++i)
    if (ops[i].kind == IrKind::Label && !labels.emplace(ops[i].name, i).second)
      return result;
  const auto input = lower_ir_to_machine(ops);
  const auto input_flow = build_post_layout_control_flow(input, control_options);
  if (!input_flow.proved) return result;
  const auto old_machine = machine_indexes(ops);
  for (std::size_t part = 0; part < parts.size(); ++part) {
    for (std::size_t source = parts[part].begin; source < parts[part].end; ++source) {
      const auto& condition = ops[source];
      if (condition.kind != IrKind::CondJump || condition.meta.manual_interaction ||
          source + 1U == parts[part].end)
        continue;
      int inverse = -1;
      std::string predicate;
      switch (condition.opcode) {
      case 0x57: inverse = 0x5e; predicate = "==0"; break;
      case 0x5e: inverse = 0x57; predicate = "!=0"; break;
      case 0x59: inverse = 0x5c; predicate = "<0"; break;
      case 0x5c: inverse = 0x59; predicate = ">=0"; break;
      default: continue;
      }
      const auto* target = std::get_if<std::string>(&condition.target);
      if (!target || !labels.contains(*target)) continue;
      auto cut = labels.at(*target);
      while (cut > 0 && ops[cut - 1U].kind == IrKind::Label) --cut;
      const auto found = std::find_if(parts.begin(), parts.end(),
          [&](const auto& candidate) { return candidate.begin == cut; });
      if (found == parts.end()) continue;
      const auto other = static_cast<std::size_t>(found - parts.begin());
      if (other == 0 || other == part || found->cells == 0) continue;
      // Manual protocols stay anchored in their original physical component.
      const auto manual = [&](std::size_t begin, std::size_t end) {
        return std::any_of(ops.begin() + static_cast<std::ptrdiff_t>(begin),
                           ops.begin() + static_cast<std::ptrdiff_t>(end),
            [](const auto& op) { return op.meta.manual_interaction.has_value(); });
      };
      if (manual(source + 1U, parts[part].end) ||
          manual(parts[other].begin, parts[other].end) ||
          !prove_post_layout_direct_conditional_inversion_effects(
              input, input_flow, old_machine[source]))
        continue;

      std::string continuation = "__conditional_region_" + std::to_string(source);
      while (labels.contains(continuation)) continuation += '_';
      auto flipped = condition;
      flipped.opcode = inverse;
      flipped.condition = predicate;
      flipped.target = continuation;
      flipped.meta.mnemonic = opcode_by_code(inverse).name;
      std::vector<IrOp> rewritten;
      std::vector<std::size_t> mapping(ops.size(), ops.size());
      const auto append = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
          mapping[i] = rewritten.size();
          rewritten.push_back(i == source ? flipped : ops[i]);
        }
      };
      for (std::size_t current = 0; current < parts.size(); ++current) {
        if (current == part) {
          append(parts[part].begin, source + 1U);
          append(parts[other].begin, parts[other].end);
        } else if (current == other) {
          IrOp label;
          label.kind = IrKind::Label;
          label.name = continuation;
          rewritten.push_back(std::move(label));
          append(source + 1U, parts[part].end);
        } else {
          append(parts[current].begin, parts[current].end);
        }
      }
      const auto output = lower_ir_to_machine(rewritten);
      const auto new_machine = machine_indexes(rewritten);
      std::vector<std::optional<std::size_t>> item_mapping(input.size());
      for (std::size_t i = 0; i < ops.size(); ++i) {
        const auto count = ops[i].kind == IrKind::Label ? 1U
            : static_cast<std::size_t>(cells_per_op(ops[i]));
        for (std::size_t cell = 0; cell < count; ++cell)
          item_mapping[old_machine[i] + cell] = new_machine[mapping[i]] + cell;
      }
      PostLayoutExecutionRelocationOptions transport;
      transport.inverted_direct_conditional_items.push_back(old_machine[source]);
      if (!prove_post_layout_execution_relocation(
              input, output, input_flow,
              build_post_layout_control_flow(output, control_options),
              item_mapping, transport).proved)
        continue;
      result.push_back({.ops = std::move(rewritten), .applied = 1,
          .optimizations = {{"conditional-region-exchange",
              "Exchanged a complementary direct condition's closed arms without adding "
              "cells; proved labelled edge polarity, all return contexts, numeric-entry "
              "and X2 convergence. Placement and selector bindings remain joint obligations."}}});
      if (result.size() == maximum_candidates) return result;
    }
  }
  return result;
}

std::vector<PassResult> stable_target_split_bridge_candidates(
    const std::vector<IrOp>& ops, const std::vector<PreloadReport>& preloads,
    const std::map<std::string, int>& encoded_targets,
    const PassContext& context, const PostLayoutControlFlowOptions& control_options,
    std::size_t maximum_candidates) {
  std::vector<PassResult> result;
  if (maximum_candidates == 0 || !symbolic(ops, true) ||
      control_options.address_space_model != flow_options(context).address_space_model)
    return result;
  const auto input = lower_ir_to_machine(ops);
  const auto flow = build_post_layout_control_flow(input, control_options);
  if (!flow.proved)
    return result;
  std::map<std::string, const PreloadReport*> values;
  for (const auto& preload : preloads)
    if (!values.emplace(preload.register_name, &preload).second)
      return result;
  std::map<std::string, std::size_t> labels;
  for (std::size_t i = 0; i < input.size(); ++i)
    if (input[i].kind == MachineItemKind::Label && !labels.emplace(input[i].name, i).second)
      return result;

  std::set<std::pair<int, std::size_t>> visited;
  for (const auto& [source, destinations] : flow.indirect_flow_targets) {
    const auto& consumer = input[source];
    const int reg = consumer.opcode & 15;
    if (reg < 7 || reg > 14 || destinations.size() != 1U ||
        !consumer.indirect_flow_targets || consumer.indirect_flow_targets->size() != 1U ||
        std::find(consumer.roles.begin(), consumer.roles.end(),
                  "late-decimal-selector-consumer") != consumer.roles.end())
      continue;
    const auto* label = std::get_if<std::string>(&consumer.indirect_flow_targets->front());
    const std::string name(1, "0123456789abcde"[reg]);
    if (!label || !labels.contains(*label) || !encoded_targets.contains(*label) ||
        !values.contains(name))
      continue;
    const auto& preload = *values.at(name);
    const auto decoded = evaluate_indirect_address(
        name, std::string_view(preload.value), IndirectOperationKind::Flow,
        control_options.address_space_model);
    if (!decoded || !decoded->formal_address ||
        decoded->formal_address->kind != FormalAddressKind::Official ||
        !indirect_writeback_preserves_literal_word(*decoded, preload.value) ||
        !rebind_stable_preloaded_indirect_flow_selector(
            input, preload, flow, encoded_targets.at(*label), encoded_targets.at(*label),
            control_options.address_space_model))
      continue;
    const auto& jump_effect = opcode_by_code(0x80 + reg);
    if (jump_effect.stack_effect != StackEffect::Preserves ||
        jump_effect.x2_effect != X2Effect::Preserves)
      continue;
    auto cut = labels.at(*label);
    while (cut > 0 && input[cut - 1U].kind == MachineItemKind::Label) --cut;
    auto target = cut;
    while (target < input.size() && input[target].kind == MachineItemKind::Label) ++target;
    if (cut == 0 || target == input.size() ||
        !visited.emplace(reg, target).second || input[target].manual_interaction ||
        input[cut - 1U].kind != MachineItemKind::Op ||
        input[cut - 1U].manual_interaction)
      continue;
    const auto& previous = input[cut - 1U];
    if (previous.opcode == 0x50 || previous.opcode == 0x29 ||
        previous.opcode == 0x51 || previous.opcode == 0x52 ||
        previous.opcode == 0x53 || (previous.opcode & 0xf0) == 0x80 ||
        (previous.opcode & 0xf0) == 0xa0)
      continue;
    bool fallthrough = false;
    for (std::size_t state = 0; state < flow.execution_states.size(); ++state) {
      if (flow.execution_states[state].item_index == cut - 1U)
        for (const auto& edge : flow.execution_edges[state])
          fallthrough = fallthrough ||
              (edge.kind == PostLayoutExecutionEdgeKind::Fallthrough &&
               flow.execution_states[edge.target_state].item_index == target);
    }
    if (!fallthrough)
      continue;

    // First prove a transparent direct bridge in reverse: only that added
    // jump is bypassed. External entries and every return frame still map to
    // the original command, not to a newly manufactured continuation.
    auto direct = input;
    direct.insert(direct.begin() + static_cast<std::ptrdiff_t>(cut),
                  {MachineItem::op(0x51, opcode_by_code(0x51).name),
                   MachineItem::address(*label)});
    const auto direct_flow = build_post_layout_control_flow(direct, control_options);
    const auto lift = analyze_post_layout_automatic_entry_lift(direct, direct_flow, 100000);
    bool closed_entry = lift.has_value();
    if (lift)
      for (std::size_t state = 0; state < direct_flow.execution_states.size(); ++state)
        if (direct_flow.execution_states[state].item_index == cut)
          closed_entry = closed_entry && (*lift)[state];
    // Other entries bypass the bridge. Their number-entry state must not
    // reject this cut, nor supply a certificate for the fallthrough path.
    if (!closed_entry)
      continue;
    std::vector<std::optional<std::size_t>> back(direct.size());
    for (std::size_t i = 0; i < input.size(); ++i) back[i < cut ? i : i + 2U] = i;
    PostLayoutExecutionRelocationOptions bypass;
    bypass.allow_bypassed_direct_jumps = true;
    if (!prove_post_layout_execution_relocation(
            direct, input, direct_flow, flow, back, bypass).proved)
      continue;

    auto indirect = input;
    auto bridge = MachineItem::op(0x80 + reg, jump_effect.name);
    bridge.indirect_flow_targets = std::vector<IrTarget>{*label};
    indirect.insert(indirect.begin() + static_cast<std::ptrdiff_t>(cut), std::move(bridge));
    const auto indirect_flow = build_post_layout_control_flow(indirect, control_options);
    std::vector<std::optional<std::size_t>> forward(direct.size());
    for (std::size_t i = 0; i < direct.size(); ++i)
      if (i != cut + 1U) forward[i] = i <= cut ? i : i - 1U;
    PostLayoutExecutionRelocationOptions convert;
    convert.direct_to_indirect_flow_items.push_back(cut);
    if (!prove_post_layout_execution_relocation(
            direct, indirect, direct_flow, indirect_flow, forward, convert).proved)
      continue;
    result.push_back({.ops = raise_machine_to_ir(
                          indirect, effective_optimizer_feature_profile(context.options)),
        .applied = 1,
        .optimizations = {{"stable-target-region-split",
            "Split a fallthrough chain with one stable-selector jump; proved exact "
            "word writeback, closed numeric entry, stack/X2 and labelled return-context "
            "transport. The extra cell is a deferred layout cost, not a saving."}}});
    if (result.size() == maximum_candidates)
      break;
  }
  return result;
}

static PassResult semantic_region_folding_impl(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions* control_options, bool defer_layout) {
  const auto unchanged = [&] { return PassResult{.ops = ops}; };
  if (!symbolic(ops, defer_layout))
    return unchanged();
  const auto parts = components(ops);
  if (parts.size() < 3U)
    return unchanged();
  const std::size_t none = ops.size();
  std::vector<std::size_t> executable;
  std::vector<std::size_t> next(ops.size() + 1U, none);
  for (std::size_t i = ops.size(); i > 0U; --i)
    next[i - 1U] = ops[i - 1U].kind == IrKind::Label ? next[i] : i - 1U;
  std::map<std::string, std::size_t> labels;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].kind == IrKind::Label) {
      if (!labels.emplace(ops[i].name, next[i]).second)
        return unchanged();
    } else {
      executable.push_back(i);
    }
  }
  std::vector<std::vector<std::size_t>> successors(ops.size());
  for (const auto i : executable) {
    const auto& op = ops[i];
    const bool direct = op.kind == IrKind::Call || op.kind == IrKind::Jump ||
                        op.kind == IrKind::CondJump || op.kind == IrKind::Loop;
    if (direct) {
      const auto& label = std::get<std::string>(op.target);
      if (!labels.contains(label) || labels.at(label) == none)
        return unchanged();
      successors[i].push_back(labels.at(label));
    }
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets) {
        const auto& label = std::get<std::string>(target);
        if (!labels.contains(label) || labels.at(label) == none)
          return unchanged();
        successors[i].push_back(labels.at(label));
      }
    if (!ends_chain(op) && next[i + 1U] != none)
      successors[i].push_back(next[i + 1U]);
  }
  // Greatest fixed point of effect-labelled, ordered successor equivalence.
  // Calls include both the callee and the exact continuation; returns are
  // transported later through the authoritative matched-context graph.
  std::vector<std::size_t> classes(ops.size(), 0);
  std::size_t work = 0;
  for (std::size_t round = 0; round <= executable.size(); ++round) {
    std::vector<std::size_t> representatives;
    auto refined = classes;
    for (const auto i : executable) {
      bool found = false;
      for (const auto representative : representatives) {
        if (++work > 1000000U)
          return unchanged();
        if (observation(ops[i]) || observation(ops[representative]) ||
            classes[i] != classes[representative] ||
            effect_fields(ops[i]) != effect_fields(ops[representative]) ||
            successors[i].size() != successors[representative].size())
          continue;
        bool same = true;
        for (std::size_t edge = 0; edge < successors[i].size(); ++edge)
          same = same && classes[successors[i][edge]] == classes[successors[representative][edge]];
        if (same) {
          refined[i] = refined[representative];
          found = true;
          break;
        }
      }
      if (!found) {
        refined[i] = representatives.size();
        representatives.push_back(i);
      }
    }
    if (refined == classes)
      break;
    classes = std::move(refined);
  }
  std::vector<std::vector<std::size_t>> commands(parts.size());
  for (std::size_t part = 0; part < parts.size(); ++part)
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      if (ops[i].kind != IrKind::Label)
        commands[part].push_back(i);
  std::vector<std::size_t> representative(parts.size());
  std::iota(representative.begin(), representative.end(), 0U);
  int saved = 0;
  for (std::size_t part = 1; part < parts.size(); ++part) {
    if (commands[part].empty() ||
        std::any_of(commands[part].begin(), commands[part].end(),
                    [&](std::size_t i) { return observation(ops[i]); }))
      continue;
    for (std::size_t keep = 1; keep < part; ++keep) {
      if (representative[keep] != keep || commands[keep].size() != commands[part].size())
        continue;
      bool same = true;
      for (std::size_t k = 0; k < commands[part].size(); ++k)
        same = same && classes[commands[part][k]] == classes[commands[keep][k]];
      if (same) {
        representative[part] = keep;
        saved += parts[part].cells;
        break;
      }
    }
  }
  if (saved == 0)
    return unchanged();
  std::vector<std::size_t> canonical(ops.size());
  std::iota(canonical.begin(), canonical.end(), 0U);
  std::map<std::size_t, std::vector<std::size_t>> aliases;
  std::map<std::size_t, std::vector<std::uint64_t>> origins;
  for (std::size_t part = 0; part < parts.size(); ++part) {
    const auto keep = representative[part];
    if (keep == part)
      continue;
    std::size_t command = 0;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      if (command >= commands[keep].size())
        return unchanged();
      const auto at = commands[keep][command];
      canonical[i] = at;
      if (ops[i].kind == IrKind::Label) {
        aliases[at].push_back(i);
      } else {
        auto& merged = origins[at];
        merged.insert(merged.end(), ops[i].meta.semantic_call_origins.begin(),
                      ops[i].meta.semantic_call_origins.end());
        ++command;
      }
    }
  }
  std::vector<IrOp> rewritten;
  std::vector<std::size_t> mapping(ops.size(), none);
  for (std::size_t part = 0; part < parts.size(); ++part) {
    if (representative[part] != part)
      continue;
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i) {
      for (const auto alias : aliases[i]) {
        mapping[alias] = rewritten.size();
        rewritten.push_back(ops[alias]);
      }
      mapping[i] = rewritten.size();
      auto op = ops[i];
      auto& merged = op.meta.semantic_call_origins;
      merged.insert(merged.end(), origins[i].begin(), origins[i].end());
      std::sort(merged.begin(), merged.end());
      merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
      rewritten.push_back(std::move(op));
    }
  }
  for (std::size_t i = 0; i < ops.size(); ++i)
    if (mapping[i] == none)
      mapping[i] = mapping[canonical[i]];
  if ((!defer_layout && !fixed_indirect_targets(ops, rewritten)) ||
      !relocated(ops, rewritten, mapping, context, control_options))
    return unchanged();
  return PassResult{.ops = std::move(rewritten), .applied = 1,
      .optimizations = {{.name = "semantic-multi-entry-region-folding",
          .detail = "Merged effect-bisimilar CFG regions, retaining every labelled entry, "
                    "matched return context and call provenance; identical executed opcodes "
                    "preserve stack/X1/X2. Removed " + std::to_string(saved) + " cells."}}};
}

PassResult semantic_region_folding(const std::vector<IrOp>& ops,
                                   const PassContext& context) {
  return semantic_region_folding_impl(ops, context, nullptr, false);
}

PassResult semantic_region_folding_with_deferred_layout(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control_options) {
  return semantic_region_folding_impl(ops, context, &control_options, true);
}

std::vector<RegisterWebRecoloringRequest> semantic_region_register_requests(
    const std::vector<IrOp>& ops, bool preserve_compiler_selector_charges) {
  std::vector<RegisterWebRecoloringRequest> result;
  if (!symbolic(ops, preserve_compiler_selector_charges))
    return result;
  const auto parts = components(ops);
  std::vector<std::vector<std::size_t>> commands(parts.size());
  for (std::size_t part = 1; part < parts.size(); ++part)
    for (std::size_t i = parts[part].begin; i < parts[part].end; ++i)
      if (ops[i].kind != IrKind::Label)
        commands[part].push_back(i);
  for (std::size_t left = 1; left < parts.size(); ++left) {
    for (std::size_t right = left + 1U; right < parts.size(); ++right) {
      if (commands[left].size() < 2U || commands[left].size() != commands[right].size())
        continue;
      RegisterWebRecoloringRequest request;
      bool compatible = true;
      for (std::size_t at = 0; at < commands[left].size(); ++at) {
        const auto a_index = commands[left][at];
        const auto b_index = commands[right][at];
        auto a = ops[a_index];
        auto b = ops[b_index];
        if (a.kind == b.kind && (a.kind == IrKind::Store || a.kind == IrKind::Recall)) {
          if (a.register_name != b.register_name)
            request.emplace_back(a_index, b_index);
          a.register_name.clear();
          b.register_name.clear();
          a.opcode &= 0xf0;
          b.opcode &= 0xf0;
        }
        compatible = compatible && !observation(a) && !observation(b) &&
                     effect_fields(a) == effect_fields(b);
      }
      if (compatible && !request.empty()) {
        result.push_back(std::move(request));
        if (result.size() == 6U)
          return result;
      }
    }
  }
  return result;
}

IrPass semantic_region_folding_pass() {
  return {.name = "semantic-multi-entry-region-folding", .run = semantic_region_folding,
          .layout_safe = false};
}

} // namespace mkpro::core::passes
