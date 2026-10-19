#include "mkpro/core/passes/contextual_copy_layout.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/detail/exact_value_terms.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <string_view>
#include <set>
#include <utility>

namespace mkpro::core::passes {
namespace {
using Id = std::size_t;

bool provenance_only(const std::vector<CellRole>& roles) {
  return std::all_of(roles.begin(), roles.end(), detail::is_cell_origin_provenance);
}

bool stock_register(std::string_view name) {
  return name.size() == 1U && std::string_view{"0123456789abcde"}.find(name[0]) !=
      std::string_view::npos;
}

bool transfer(const IrOp& op) {
  if ((op.kind != IrKind::Store && op.kind != IrKind::Recall) ||
      op.meta.raw || op.meta.manual_interaction || op.meta.logical_register_analysis ||
      !stock_register(op.register_name)) return false;
  const int base = op.kind == IrKind::Store ? 0x40 : 0x60;
  return op.opcode == base + register_index(op.register_name);
}

bool copy(const std::vector<IrOp>& ops, Id store) {
  return store > 0 && store < ops.size() && transfer(ops[store - 1U]) &&
      transfer(ops[store]) && ops[store - 1U].kind == IrKind::Recall &&
      ops[store].kind == IrKind::Store && !ops[store - 1U].procedure_boundary &&
      !ops[store].procedure_boundary && ops[store - 1U].meta.semantic_call_origins.empty() &&
      ops[store].meta.semantic_call_origins.empty() &&
      provenance_only(ops[store - 1U].meta.roles) && provenance_only(ops[store].meta.roles) &&
      provenance_only(ops[store - 1U].target_meta.roles) && provenance_only(ops[store].target_meta.roles) &&
      ops[store - 1U].register_name != ops[store].register_name;
}

struct Model {
  const std::vector<IrOp>& ops;
  std::vector<MachineItem> items;
  AuthoritativePostLayoutControlFlow flow;
  std::vector<Id> instruction, item;
  std::vector<int> address;
  std::vector<std::vector<Id>> predecessors;
  std::vector<RegisterEffects> effects;
  std::map<std::string, int> positions;
  PostLayoutExternalEntryContexts entries;
  LivenessOptions live;
  bool valid = false;
  bool precise_entry = false;

  Model(const std::vector<IrOp>& source, AuthoritativePostLayoutControlFlow control,
        const std::vector<PreloadReport>& preloads) : ops(source), items(lower_ir_to_machine(source)),
        flow(std::move(control)) {
    if (!flow.proved || !prove_post_layout_input_boundaries(items, flow).proved) return;
    const auto roots = resolve_post_layout_external_entry_contexts(flow);
    if (!roots) return;
    entries = *roots;
    std::map<int, Id> at;
    int pc = 0;
    Id machine = 0;
    for (Id i = 0; i < ops.size(); ++i) {
      if (ops[i].meta.raw || ops[i].meta.logical_register_analysis ||
          ops[i].kind == IrKind::OrphanAddress) return;
      address.push_back(pc);
      item.push_back(machine);
      effects.push_back(register_effects(ops[i]));
      const auto& effect = effects.back();
      for (const auto* names : {&effect.uses, &effect.must_defs, &effect.may_defs})
        if (!std::all_of(names->begin(), names->end(), stock_register)) return;
      if ((ops[i].kind == IrKind::Store || ops[i].kind == IrKind::Recall) &&
          (!stock_register(ops[i].register_name) || ops[i].opcode !=
              (ops[i].kind == IrKind::Store ? 0x40 : 0x60) +
                  register_index(ops[i].register_name))) return;
      if (ops[i].opcode == 0x4f || ops[i].opcode == 0x6f ||
          (ops[i].opcode >= 0x70 && (ops[i].opcode & 15) == 15)) return;
      if (ops[i].kind == IrKind::Label) {
        if (!positions.emplace(ops[i].name, pc).second) return;
      } else at.emplace(pc, i);
      pc += cells_per_op(ops[i]);
      machine += lower_ir_to_machine({ops[i]}).size();
    }
    predecessors.resize(flow.execution_states.size());
    for (Id s = 0; s < flow.execution_states.size(); ++s) {
      const auto found = at.find(flow.execution_states[s].address);
      if (found == at.end()) return;
      instruction.push_back(found->second);
      for (auto next : flow.execution_successors[s]) {
        if (next >= predecessors.size()) return;
        predecessors[next].push_back(s);
      }
    }
    for (const auto& preload : preloads) {
      if (!stock_register(preload.register_name)) return;
      if (!preload.setup_expression && !preload.setup_target_name && !preload.setup_expression_text)
        live.equal_entry_value_classes[preload.register_name] = "literal:" + preload.value;
    }
    valid = true;
  }
};

bool memory_observation(const IrOp& op, const RegisterEffects& effect,
                        const std::string& reg) {
  if (effect.uses_all_registers || effect.uses.contains(reg)) return true;
  if (op.meta.manual_interaction)
    return op.meta.manual_interaction->kind == ManualInteractionAnchorKind::PromptStop &&
        std::find(op.meta.roles.begin(), op.meta.roles.end(), kTypedDisplayObservationRole) ==
            op.meta.roles.end();
  return op.kind == IrKind::Stop &&
      std::find(op.meta.roles.begin(), op.meta.roles.end(), kTypedDisplayObservationRole) ==
          op.meta.roles.end();
}

bool path_identity(const std::vector<IrOp>& path, int reg,
                   const OrdinaryNumericRange& range) {
  if (reg < 0 || reg >= 15 || !range.integral || range.lower < -99999999 ||
      range.upper > 99999999 || range.lower > range.upper ||
      range.upper - range.lower >= 16) return false;
  for (auto value = range.lower; value <= range.upper; ++value) {
    detail::exact_terms::Evaluation evaluation;
    const auto word = detail::exact_terms::number(std::to_string(value));
    if (!word) return false;
    evaluation.state.registers[static_cast<Id>(reg)] = *word;
    Id flow_epoch = 0;
    for (const auto& op : path) {
      if (op.kind == IrKind::Label) continue;
      const bool indirect = op.kind == IrKind::IndirectCall || op.kind == IrKind::IndirectJump ||
          op.kind == IrKind::IndirectCondJump;
      if (op.kind == IrKind::Call || op.kind == IrKind::Return || op.kind == IrKind::Jump ||
          op.kind == IrKind::CondJump || indirect) {
        if (indirect) {
          const int selector = register_index(op.register_name);
          if (selector < 7 || selector >= 15 || selector == reg) return false;
          // Value equality must not accidentally rely on a selector's apparent
          // spelling surviving its hardware write-back.
          evaluation.state.registers[static_cast<Id>(selector)] =
              detail::exact_terms::symbolic("selector-write-" + std::to_string(flow_epoch++));
        }
        evaluation.synchronize_call();
      } else evaluation.execute(op);
    }
    if (!evaluation.valid || evaluation.state.stack[0] != *word ||
        evaluation.state.registers[static_cast<Id>(reg)] != *word) return false;
  }
  return true;
}

ContextualCopyStoreProof prove_store(const Model& model, Id store,
    const std::vector<PreloadReport>& preloads, Id maximum_work) {
  ContextualCopyStoreProof result;
  if (!model.valid || !copy(model.ops, store) || maximum_work == 0) return result;
  const auto& ops = model.ops;
  const auto& reg = ops[store].register_name;
  const auto spend = [&] {
    if (result.work >= maximum_work) { result.exhausted = true; return false; }
    ++result.work;
    return true;
  };
  struct Context {
    Id state = 0;
    std::vector<GuardedDefinitionUseQuery> observations, definitions;
    std::vector<IrOp> path;
    bool unknown = false;
    bool recalled = false;
  };
  std::vector<Context> contexts;
  std::set<GuardedDefinitionUseQuery> requests;
  for (Id root = 0; root < model.instruction.size(); ++root) {
    if (!spend()) return result;
    if (model.instruction[root] != store) continue;
    Context context;
    context.state = root;

    bool structural_dead = true;
    std::set<Id> live_states;
    std::deque<Id> after(model.flow.execution_successors[root].begin(), model.flow.execution_successors[root].end());
    while (!after.empty()) {
      if (!spend()) return result;
      const auto s = after.front(); after.pop_front();
      if (!live_states.insert(s).second) continue;
      const auto i = model.instruction[s];
      const auto& effect = model.effects[i];
      if (memory_observation(ops[i], effect, reg)) { structural_dead = false; break; }
      if (effect.must_defs.contains(reg)) continue;
      after.insert(after.end(), model.flow.execution_successors[s].begin(), model.flow.execution_successors[s].end());
    }
    for (Id use = 0; !structural_dead && use < ops.size(); ++use)
      if (memory_observation(ops[use], model.effects[use], reg)) {
        context.observations.push_back({store, use, reg, root, {}});
        requests.insert(context.observations.back());
      }
    std::set<Id> seen;
    std::deque<Id> pending(model.predecessors[root].begin(), model.predecessors[root].end());
    if (pending.empty()) context.unknown = true;
    while (!pending.empty()) {
      if (!spend()) return result;
      const auto state = pending.front(); pending.pop_front();
      if (!seen.insert(state).second) continue;
      const auto& effect = model.effects[model.instruction[state]];
      if (model.entries.main[state] || effect.may_defs.contains(reg) ||
          effect.may_define_any_register || model.predecessors[state].empty()) {
        context.unknown = true;
        break;
      }
      if (effect.must_defs.contains(reg)) {
        context.definitions.push_back({model.instruction[state], store, reg, state, root});
        requests.insert(context.definitions.back());
        continue;
      }
      pending.insert(pending.end(), model.predecessors[state].begin(), model.predecessors[state].end());
    }
    auto cursor = root;
    seen.clear();
    for (int length = 0; length < 96 && seen.insert(cursor).second; ++length) {
      if (!spend()) return result;
      if (model.predecessors[cursor].size() != 1U) break;
      cursor = model.predecessors[cursor].front();
      const auto& op = ops[model.instruction[cursor]];
      const auto& effect = model.effects[model.instruction[cursor]];
      if (effect.must_defs.contains(reg) || effect.may_defs.contains(reg) ||
          effect.may_define_any_register || op.meta.manual_interaction) break;
      context.path.push_back(op);
      if (op.kind == IrKind::Recall && op.register_name == reg) {
        context.recalled = true;
        break;
      }
    }
    std::reverse(context.path.begin(), context.path.end());
    contexts.push_back(std::move(context));
  }
  if (contexts.empty() || requests.size() > 64U) return result;
  const std::vector<GuardedDefinitionUseQuery> queries(requests.begin(), requests.end());
  const auto guards = analyze_unreachable_definition_uses(
      ops, model.flow, queries, model.live, maximum_work - result.work);
  result.work += guards.work;
  if (guards.exhausted) { result.exhausted = true; return result; }
  const auto excluded = [&](const auto& query) { return guards.excluded_uses.contains(query); };
  std::vector<Id> bounded_definitions;
  for (const auto& context : contexts) {
    if (std::all_of(context.observations.begin(), context.observations.end(), excluded)) continue;
    if (context.unknown || context.definitions.empty()) return result;
    if (std::all_of(context.definitions.begin(), context.definitions.end(), excluded)) continue;
    if (!context.recalled) return result;
    for (const auto& query : context.definitions)
      if (!excluded(query)) bounded_definitions.push_back(model.item[query.definition]);
  }
  std::sort(bounded_definitions.begin(), bounded_definitions.end());
  bounded_definitions.erase(std::unique(bounded_definitions.begin(), bounded_definitions.end()),
                            bounded_definitions.end());
  PostLayoutNumericRangeProof ranges;
  if (!bounded_definitions.empty()) {
    auto lift = model.precise_entry
        ? analyze_post_layout_automatic_entry_lift_with_indirect_store_facts(
            model.items, model.flow, maximum_work - result.work)
        : analyze_post_layout_automatic_entry_lift(model.items, model.flow, maximum_work - result.work);
    if (!lift) return result;
    ranges = prove_post_layout_numeric_ranges(model.items, model.flow, *lift,
        bounded_definitions, &preloads, maximum_work - result.work,
        NumericRangeInputPolicy::ValidatedRegisterTransport);
    result.work += ranges.work;
    if (ranges.exhausted) { result.exhausted = true; return result; }
  }
  for (const auto& context : contexts) {
    if (std::all_of(context.observations.begin(), context.observations.end(), excluded)) {
      ++result.dead_contexts;
      continue;
    }
    if (std::all_of(context.definitions.begin(), context.definitions.end(), excluded)) {
      ++result.unreachable_contexts;
      continue;
    }
    std::optional<OrdinaryNumericRange> bound;
    for (const auto& query : context.definitions) {
      if (excluded(query)) continue;
      const auto found = ranges.x_before.find(model.item[query.definition]);
      if (found == ranges.x_before.end() || !found->second.integral) return result;
      if (!bound) bound = found->second;
      else { bound->lower = std::min(bound->lower, found->second.lower);
             bound->upper = std::max(bound->upper, found->second.upper); }
    }
    if (!bound || !bound->integral || bound->lower > bound->upper ||
        bound->upper - bound->lower >= 16) return result;
    const auto work = static_cast<Id>(bound->upper - bound->lower + 1) * context.path.size();
    if (work > maximum_work - result.work) { result.exhausted = true; return result; }
    if (!path_identity(context.path, register_index(reg), *bound)) return result;
    result.work += work;
    ++result.fixed_point_contexts;
  }
  result.proved = true;
  return result;
}

struct Rewrite {
  std::vector<IrOp> ops;
  std::vector<std::optional<Id>> mapping;
  std::optional<Id> inserted_jump;
};

// Comments classify compiler-generated selector-entry obligations. Crossing
// the matching entry store turns an old entry transfer into an internal BP;
// preserving that obsolete classification would invent a second charge site.
std::optional<std::pair<std::string, std::string>> entry_contract(
    const std::optional<std::string>& comment, std::string_view marker) {
  if (!comment || !comment->starts_with(marker)) return std::nullopt;
  const auto field = [&](std::string_view key) -> std::optional<std::string> {
    const std::string token = "; " + std::string(key) + "=";
    const auto found = comment->find(token, marker.size() - 1U);
    if (found == std::string::npos || comment->find(token, found + token.size()) != std::string::npos)
      return std::nullopt;
    const auto begin = found + token.size();
    const auto end = comment->find_first_of("; \t\r\n", begin);
    if (begin == comment->size() || end == begin) return std::nullopt;
    return comment->substr(begin, end == std::string::npos ? end : end - begin);
  };
  const auto proof = field("proof"), selector = field("selector");
  if (!proof || !selector || selector->size() != 1U) return std::nullopt;
  return std::pair{*proof, *selector};
}

Rewrite rewrite(const Model& model, Id store, std::optional<Id> jump = {},
                Id begin = 0, Id cut = 0) {
  Rewrite result;
  const auto& ops = model.ops;
  std::vector<std::optional<Id>> operations(ops.size());
  std::optional<Id> inserted;
  std::string label = "__contextual_copy_tail";
  while (model.positions.contains(label)) label += '_';
  const auto append = [&](Id i) {
    operations[i] = result.ops.size();
    result.ops.push_back(ops[i]);
  };
  for (Id i = 0; i < ops.size(); ++i) {
    if (jump && i == cut) { IrOp marker; marker.kind = IrKind::Label; marker.name = label;
                            result.ops.push_back(std::move(marker)); }
    if (i == store - 1U || i == store) continue;
    if (jump && i == *jump) {
      for (Id k = begin; k < cut; ++k) append(k);
      inserted = result.ops.size();
      auto moved = ops[i]; moved.target = label;
      const auto entering = entry_contract(moved.meta.comment,
          "callee-hole charge-entry tail transfer;");
      auto prefix = begin;
      while (prefix < cut && ops[prefix].kind == IrKind::Label) ++prefix;
      if (entering && prefix < cut && ops[prefix].kind == IrKind::Store &&
          ops[prefix].register_name == entering->second &&
          entry_contract(ops[prefix].meta.comment,
              "callee-hole charge-entry store;") == entering)
        moved.meta.comment = "internal continuation after a charged prefix";
      result.ops.push_back(std::move(moved));
      continue;
    }
    if (jump && i >= begin && i < cut) continue;
    append(i);
  }
  std::vector<Id> items;
  Id machine = 0;
  for (const auto& op : result.ops) {
    items.push_back(machine);
    machine += lower_ir_to_machine({op}).size();
  }
  result.mapping.resize(model.items.size());
  for (Id i = 0; i < ops.size(); ++i)
    if (operations[i])
      for (Id cell = 0; cell < lower_ir_to_machine({ops[i]}).size(); ++cell)
        result.mapping[model.item[i] + cell] = items[*operations[i]] + cell;
  Id retained = store + 1U;
  while (retained < ops.size() && ops[retained].kind == IrKind::Label) ++retained;
  if (retained == ops.size() || !operations[retained]) { result.ops.clear(); return result; }
  result.mapping[model.item[store - 1U]] = items[*operations[retained]];
  result.mapping[model.item[store]] = items[*operations[retained]];
  if (inserted) result.inserted_jump = items[*inserted];
  return result;
}

bool pinned(const Model& model, const std::vector<IrOp>& ops) {
  std::map<std::string, int> positions;
  int address = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label && !positions.emplace(op.name, address).second) return false;
    address += cells_per_op(op);
  }
  for (const auto& op : model.ops)
    if (op.meta.indirect_flow_targets)
      for (const auto& target : *op.meta.indirect_flow_targets) {
        const auto* label = std::get_if<std::string>(&target);
        if (!label || !positions.contains(*label) || !model.positions.contains(*label) ||
            positions.at(*label) != model.positions.at(*label)) return false;
      }
  return true;
}

void erase_copies(const std::vector<IrOp>& ops, const PassContext& context,
                  const ContextualCopyLayoutOptions& limits, std::vector<PassResult>& out) {
  PostLayoutControlFlowOptions control;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  control.empty_return_target = 1;
  const auto source_items = lower_ir_to_machine(ops);
  const auto source_control = build_post_layout_control_flow(source_items, control);
  if (!source_control.proved) return;
  const auto selected = build_selector_consistent_execution_graph(source_items, source_control);
  if (!selected.proved) return;
  auto data_control = source_control;
  data_control.execution_states.clear();
  for (const auto state : selected.original_states)
    data_control.execution_states.push_back(source_control.execution_states[state]);
  data_control.execution_successors = selected.successors;
  data_control.execution_edges = selected.execution_edges;
  Model model(ops, std::move(data_control), *limits.runtime_preloads);
  if (!model.valid) return;
  Id attempts = 0, remaining = limits.maximum_work;
  for (Id store = 1; store < ops.size() && remaining && out.size() < limits.maximum_candidates; ++store) {
    if (!copy(ops, store)) continue;
    if (attempts++ >= limits.maximum_copy_sites) return;
    const auto data = prove_store(model, store, *limits.runtime_preloads, remaining);
    remaining -= std::min(remaining, data.work);
    if (!data.proved || data.exhausted) continue;
    // The retained predecessor, not the soon-to-be-erased recall, must already
    // close numeric entry. Otherwise deletion could merge adjacent digits or
    // leave the exponent-entry mode active before the common continuation.
    const auto entry_lift = analyze_post_layout_automatic_entry_lift(
        model.items, model.flow, remaining);
    if (!entry_lift) continue;
    bool closed = true;
    for (Id state = 0; state < model.instruction.size(); ++state)
      if (model.instruction[state] == store - 1U && !entry_lift->at(state)) closed = false;
    if (!closed) continue;
    std::vector<Id> continuations;
    for (Id state = 0; state < model.instruction.size(); ++state)
      if (model.instruction[state] == store)
        continuations.insert(continuations.end(), model.flow.execution_successors[state].begin(),
                              model.flow.execution_successors[state].end());
    std::sort(continuations.begin(), continuations.end());
    continuations.erase(std::unique(continuations.begin(), continuations.end()), continuations.end());
    auto neutral = ops;
    neutral[store] = IrOp{};
    neutral[store].kind = IrKind::Plain;
    neutral[store].opcode = 0x54;
    if (!prove_post_layout_stack_state_equality(lower_ir_to_machine(neutral), model.flow,
            continuations, {{false, false, false, false}, false, true},
            {.refine_runtime_selectors = false, .numeric_entry_closed = true,
             .transport_closed_entry_through_control = true})) continue;
    const auto accept = [&](Rewrite candidate, bool interchange) {
      if (candidate.ops.empty() || !pinned(model, candidate.ops)) return;
      const auto image = lower_ir_to_machine(candidate.ops);
      const auto final = build_post_layout_control_flow(image, control);
      PostLayoutExecutionRelocationOptions relocation;
      relocation.erased_data_effect_items = {model.item[store - 1U], model.item[store]};
      relocation.allow_bypassed_direct_jumps = interchange;
      if (candidate.inserted_jump) relocation.inserted_transparent_jump_items = {*candidate.inserted_jump};
      if (!prove_post_layout_execution_relocation(source_items, image, source_control, final,
                                                   candidate.mapping, relocation).proved) return;
      auto closed = rebind_layout_search_artifact({image, *limits.runtime_preloads, {}});
      if (!closed || closed->preloads.size() != limits.runtime_preloads->size()) return;
      for (Id i = 0; i < closed->preloads.size(); ++i)
        if (closed->preloads[i].register_name != (*limits.runtime_preloads)[i].register_name ||
            closed->preloads[i].value != (*limits.runtime_preloads)[i].value) return;
      const auto source_cells = std::count_if(source_items.begin(), source_items.end(), [](const auto& m) {
        return m.kind != MachineItemKind::Label;
      });
      const auto final_cells = std::count_if(closed->items.begin(), closed->items.end(), [](const auto& m) {
        return m.kind != MachineItemKind::Label;
      });
      if (final_cells >= source_cells) return;
      PassResult result;
      result.ops = raise_machine_to_ir(closed->items, effective_optimizer_feature_profile(context.options));
      result.applied = static_cast<int>(source_cells - final_cells);
      result.optimizations.push_back({"joint-contextual-copy-layout",
          "Composed exact register-web allocation, dead-or-fixed-point copy elimination and "
          "shared-prefix layout. Certified " + std::to_string(data.dead_contexts) +
          " dead, " + std::to_string(data.fixed_point_contexts) + " fixed-point and " +
          std::to_string(data.unreachable_contexts) + " unreachable caller contexts. " +
          (interchange ? "Interchanged an unconditional jump with unchanged direct transfers; " : "") +
          "preserved typed input, final selector words, rooted CFG, stack/X1/X2 and return frames."});
      out.push_back(std::move(result));
    };
    accept(rewrite(model, store), false);
    if (out.size() >= limits.maximum_candidates) return;
    for (Id jump = 0; jump < ops.size() && out.size() < limits.maximum_candidates; ++jump) {
      const auto& op = ops[jump];
      if (op.kind != IrKind::Jump || op.meta.raw || op.meta.manual_interaction ||
          op.target_meta.formal_opcode || !provenance_only(op.meta.roles)) continue;
      const auto* target = std::get_if<std::string>(&op.target);
      if (!target) continue;
      const auto entry = std::find_if(ops.begin(), ops.end(), [&](const auto& candidate) {
        return candidate.kind == IrKind::Label && candidate.name == *target;
      });
      if (entry == ops.end()) continue;
      Id begin = static_cast<Id>(entry - ops.begin());
      while (begin && ops[begin - 1U].kind == IrKind::Label) --begin;
      if (begin <= jump) continue;
      Id first = begin;
      while (first < ops.size() && ops[first].kind == IrKind::Label) ++first;
      if (first == ops.size() || !transfer(ops[first])) continue;
      std::vector<Id> after_first;
      for (Id state = 0; state < model.instruction.size(); ++state)
        if (model.instruction[state] == first)
          after_first.insert(after_first.end(), model.flow.execution_successors[state].begin(),
                              model.flow.execution_successors[state].end());
      std::sort(after_first.begin(), after_first.end());
      after_first.erase(std::unique(after_first.begin(), after_first.end()), after_first.end());
      if (!prove_post_layout_stack_state_equality(model.items, model.flow, after_first,
              {{true, true, true, true}, false, true},
              {.refine_runtime_selectors = false, .numeric_entry_closed = true,
             .transport_closed_entry_through_control = true})) continue;
      Id cut = begin;
      for (Id commands = 0; commands < limits.maximum_prefix_commands && cut < ops.size(); ++commands) {
        while (cut < ops.size() && ops[cut].kind == IrKind::Label) ++cut;
        if (cut == ops.size() || !transfer(ops[cut])) break;
        ++cut;
        if (cut == ops.size() || ops[cut].kind == IrKind::Label ||
            !(store + 1U <= begin || store - 1U >= cut) ||
            (jump >= store - 1U && jump <= store)) continue;
        accept(rewrite(model, store, jump, begin, cut), true);
        if (out.size() >= limits.maximum_candidates) return;
      }
    }
  }
}
} // namespace

ContextualCopyStoreProof prove_contextual_copy_store_elision(
    const std::vector<IrOp>& ops, const AuthoritativePostLayoutControlFlow& flow,
    std::size_t store, const std::vector<PreloadReport>& preloads, std::size_t maximum_work,
    bool closed_artifact_precision) {
  Model model(ops, flow, preloads);
  model.precise_entry=closed_artifact_precision;
  model.live.discover_toggle_projection_signs=closed_artifact_precision;
  return prove_store(model, store, preloads, maximum_work);
}

std::vector<RegisterWebRecoloringRequest> contextual_copy_register_requests(
    const std::vector<IrOp>& ops, std::size_t maximum_requests) {
  std::vector<RegisterWebRecoloringRequest> requests;
  for (Id a = 1; a < ops.size() && requests.size() < maximum_requests; ++a) {
    if (!copy(ops, a)) continue;
    for (Id b = a + 1U; b < ops.size() && requests.size() < maximum_requests; ++b)
      if (copy(ops, b) && ops[b - 1U].register_name == ops[a - 1U].register_name &&
          ops[b].register_name != ops[a].register_name) requests.push_back({{a, b}});
  }
  return requests;
}

std::vector<PassResult> contextual_copy_layout_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const ContextualCopyLayoutOptions& limits) {
  std::vector<PassResult> result;
  if (!limits.runtime_preloads || !limits.maximum_candidates || !limits.maximum_work ||
      !limits.maximum_copy_sites) return result;
  const auto requests = contextual_copy_register_requests(ops);
  if (requests.empty()) {
    erase_copies(ops, context, limits, result);
    return result;
  }
  std::map<std::string, int> origins;
  int address = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label) origins.emplace(op.name, address);
    address += cells_per_op(op);
  }
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  for_each_deferred_register_web_recoloring(ops, context, requests, control, origins,
      [&](std::vector<IrOp> changed) {
        if (result.size() < limits.maximum_candidates) erase_copies(changed, context, limits, result);
      }, limits.runtime_preloads);
  return result;
}
} // namespace mkpro::core::passes
