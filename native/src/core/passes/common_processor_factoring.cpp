#include "mkpro/core/passes/common_processor_factoring.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/stack_value_equivalence.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {
namespace {

bool plain_metadata(const IrOp& op, bool input_anchor = false,
                    bool transport_body_roles = false) {
  const bool roles = op.meta.roles.empty() ||
      (transport_body_roles &&
       std::all_of(op.meta.roles.begin(), op.meta.roles.end(), [&](const auto& role) {
         if (op.kind == IrKind::Recall)
           return role.starts_with(kRetunableNaturalFractionalSelectorRolePrefix) ||
               role == "helper-invariant-recall-hoist:root" ||
               role == "helper-invariant-recall-hoist:tail";
         return op.kind == IrKind::Plain && op.opcode == 0x14 &&
             (role == "helper-invariant-recall-hoist:entry-swap" ||
              role == "helper-invariant-recall-hoist:return-swap" ||
              role == "helper-invariant-recall-hoist:caller-swap");
       }));
  return !op.meta.raw && (input_anchor || !op.meta.manual_interaction) &&
      roles && op.target_meta.roles.empty() &&
      !op.target_meta.formal_opcode && !op.meta.tactic &&
      !op.meta.logical_register_name && !op.meta.logical_register_analysis;
}

bool ordinary_register(const IrOp& op) {
  const int reg = register_index(op.register_name);
  return reg >= 0 && reg < 15;
}

struct Index {
  std::map<std::string, std::size_t> labels;
  std::vector<std::size_t> item;
  bool valid = true;
};

Index index_ops(const std::vector<IrOp>& ops) {
  Index index;
  std::size_t item = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    index.item.push_back(item);
    item += lower_ir_to_machine({ops[i]}).size();
    if (ops[i].kind == IrKind::Label) {
      std::size_t entry = i + 1;
      while (entry < ops.size() && ops[entry].kind == IrKind::Label) ++entry;
      index.valid = index.labels.emplace(ops[i].name, entry).second && index.valid;
    }
  }
  return index;
}

std::optional<std::size_t> target(const IrTarget& value, const Index& index) {
  const auto* label = std::get_if<std::string>(&value);
  if (!label || !index.labels.contains(*label)) return std::nullopt;
  return index.labels.at(*label);
}

std::optional<std::size_t> call_target(const IrOp& op, const Index& index,
                                      bool input_anchor = false) {
  if (!plain_metadata(op, input_anchor)) return std::nullopt;
  if (op.kind == IrKind::Call) return target(op.target, index);
  // The replay must not introduce another selector increment/decrement.
  if (op.kind == IrKind::IndirectCall && op.opcode >= 0xa7 && op.opcode <= 0xae &&
      op.meta.indirect_flow_targets && op.meta.indirect_flow_targets->size() == 1U &&
      !op.meta.indirect_flow_formal_targets)
    return target(op.meta.indirect_flow_targets->front(), index);
  return std::nullopt;
}

bool modeled_plain(int code) {
  return (code >= 0 && code <= 9) || code == 0x0a || code == 0x14 ||
      code == 0x25 || (code >= 0x10 && code <= 0x13) || code == 0x15 ||
      code == 0x22 || code == 0x31 || code == 0x32 ||
      (code >= 0x34 && code <= 0x39);
}

std::optional<std::vector<IrOp>> leaf(const std::vector<IrOp>& ops,
                                     std::size_t start, bool pure) {
  std::vector<IrOp> body;
  for (std::size_t i = start; i < ops.size() && body.size() < 48U; ++i) {
    const auto& op = ops[i];
    if (op.kind == IrKind::Label) continue;
    // The common body is moved unchanged. Preserve its data/address contract;
    // the symbolic proof treats the recalled word as arbitrary, and final
    // layout still owns every obligation attached to the role.
    if (!plain_metadata(op, false, !pure)) return std::nullopt;
    if (op.kind == IrKind::Return) return body.empty() ? std::nullopt : std::optional{body};
    if (op.kind == IrKind::Plain && modeled_plain(op.opcode)) {
      body.push_back(op);
    } else if (!pure && (op.kind == IrKind::Store || op.kind == IrKind::Recall) &&
               register_index(op.register_name) >= 0 && register_index(op.register_name) < 15) {
      body.push_back(op);
    } else {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Exact terms, not floating-point samples. Unknown arithmetic is kept in its
// original order. Its entire incoming machine state is also an observation:
// an error must not expose a different register or stack before convergence.
struct Value {
  std::string key;
  std::optional<std::string> decimal;
  bool operator==(const Value&) const = default;
};
Value symbolic(const std::string& name) { return {"$" + name, std::nullopt}; }
std::optional<Value> number(const std::string& value) {
  const auto exact = concrete_decimal_binary_value(0x10, value, "0");
  if (!exact) return std::nullopt;
  return Value{"#" + *exact, *exact};
}
struct State {
  std::array<Value, 4> stack;
  std::array<Value, 15> registers;
  Value x1, x2;
  std::string literal;
  bool operator==(const State&) const = default;
};
State initial() {
  State s;
  for (std::size_t i = 0; i < s.stack.size(); ++i)
    s.stack[i] = symbolic("stack" + std::to_string(i));
  for (std::size_t i = 0; i < s.registers.size(); ++i)
    s.registers[i] = symbolic("register" + std::to_string(i));
  s.x1 = symbolic("last-X");
  s.x2 = symbolic("entry-shadow");
  return s;
}
struct Observation {
  int opcode = 0;
  State before;
  bool operator==(const Observation&) const = default;
};
struct Evaluation {
  State state = initial();
  std::vector<Observation> observations;
  bool valid = true;

  void synchronize_call() {
    state.x2 = state.stack[0];
    state.literal.clear();
  }

  void execute(const IrOp& op) {
    if (!valid) return;
    const auto old = state.stack;
    const int code = op.opcode;
    if (op.kind == IrKind::Recall) {
      state.stack = {state.registers.at(static_cast<std::size_t>(register_index(op.register_name))),
                     old[0], old[1], old[2]};
      state.x2 = old[0];
      state.literal.clear();
      return;
    }
    if (op.kind == IrKind::Store) {
      state.registers.at(static_cast<std::size_t>(register_index(op.register_name))) = old[0];
      state.literal.clear();
      return;
    }
    if (op.kind != IrKind::Plain || !modeled_plain(code)) { valid = false; return; }
    if (code >= 0 && code <= 9) {
      if (state.literal.empty()) state.stack = {old[0], old[0], old[1], old[2]};
      state.literal.push_back(static_cast<char>('0' + code));
      const auto value = number(state.literal);
      if (!value || state.literal.size() > 8U) { valid = false; return; }
      state.stack[0] = *value;
      state.x2 = *value;
      return;
    }
    if (code == 0x0a) {
      if (state.literal.empty() || state.literal.find('.') != std::string::npos) {
        valid = false;
        return;
      }
      state.literal += '.';
      return;
    }
    if (code == 0x14) {
      state.stack = {old[1], old[0], old[2], old[3]};
    } else if (code == 0x25) {
      state.stack = {old[1], old[2], old[3], old[0]};
    } else {
      const bool binary = (code >= 0x10 && code <= 0x13) ||
                          (code >= 0x36 && code <= 0x39);
      Value value;
      if (old[0].decimal && (!binary || old[1].decimal)) {
        const auto exact = binary
            ? concrete_decimal_binary_value(code, *old[1].decimal, *old[0].decimal)
            : concrete_decimal_unary_value(code, *old[0].decimal);
        const auto evaluated = exact ? number(*exact) : std::nullopt;
        if (!evaluated) { valid = false; return; }
        value = *evaluated;
      } else {
        observations.push_back({code, state});
        value = {"(" + std::to_string(code) + ":" +
                 (binary ? old[1].key + "," : std::string{}) + old[0].key + ")", std::nullopt};
      }
      const auto& info = opcode_by_code(code);
      if (binary) {
        if (info.stack_effect == StackEffect::ConsumeYDrop)
          state.stack = {value, old[2], old[3], old[3]};
        else if (info.stack_effect == StackEffect::ConsumeYKeep)
          state.stack = {value, old[1], old[2], old[3]};
        else { valid = false; return; }
      } else {
        if (info.stack_effect != StackEffect::Preserves) { valid = false; return; }
        state.stack[0] = value;
      }
      if (info.x2_effect == X2Effect::Affects) state.x2 = old[0];
      else if (info.x2_effect != X2Effect::Preserves) { valid = false; return; }
    }
    if (opcode_saves_physical_x1(code)) state.x1 = old[0];
    state.literal.clear();
  }

  void call(const std::vector<IrOp>& body) {
    synchronize_call();
    for (const auto& op : body) execute(op);
    // This proof does not model a return while mantissa entry is still open.
    if (!state.literal.empty()) valid = false;
    synchronize_call();
  }
};

bool same(const Evaluation& a, const Evaluation& b) {
  return a.valid && b.valid && a.state == b.state && a.observations == b.observations;
}
bool fixed_point(const std::vector<IrOp>& body, std::int64_t value) {
  const auto n = number(std::to_string(value));
  if (!n) return false;
  Evaluation e;
  e.state.stack[0] = *n;
  const auto registers = e.state.registers;
  e.call(body);
  return e.valid && e.observations.empty() && e.state.stack[0] == *n &&
         e.state.registers == registers;
}

struct Pattern {
  std::size_t ordinary = 0, admitted = 0, helper = 0;
  std::vector<std::size_t> other_calls;
  std::vector<IrOp> normalizer, body;
  std::size_t ordinary_begin = 0, admitted_begin = 0;
  std::size_t recall_second = 0;
  std::size_t admitted_first = 0, admitted_second = 0;
  bool register_arguments = false;
  bool partitioned = false;
};


std::optional<Pattern> projection_prologue(const std::vector<IrOp>& ops,
                                          const Index& index, std::size_t call,
                                          bool register_arguments) {
  const std::size_t width = register_arguments ? 5 : 4;
  if (call < width) return std::nullopt;
  const std::size_t start = call - width;
  const auto helper = call_target(ops[call], index);
  const auto normalizer = call_target(ops[start], index, true);
  const auto same_register = [&](std::size_t a, std::size_t b) {
    return register_index(ops[a].register_name) == register_index(ops[b].register_name);
  };
  if (!helper || !normalizer || *helper == *normalizer ||
      call_target(ops[start + 3], index) != normalizer ||
      ops[start + 1].kind != IrKind::Store || ops[start + 2].kind != IrKind::Recall ||
      !plain_metadata(ops[start + 1]) || !plain_metadata(ops[start + 2]) ||
      !ordinary_register(ops[start + 1]) || !ordinary_register(ops[start + 2]) ||
      same_register(start + 1, start + 2))
    return std::nullopt;
  if (register_arguments &&
      (ops[start + 4].kind != IrKind::Store || !plain_metadata(ops[start + 4]) ||
       !ordinary_register(ops[start + 4]) || same_register(start + 4, start + 1)))
    return std::nullopt;
  if (ops[start].meta.manual_interaction &&
      ops[start].meta.manual_interaction->kind != ManualInteractionAnchorKind::ContinuousResume)
    return std::nullopt;
  const auto pure = leaf(ops, *normalizer, true);
  const auto body = leaf(ops, *helper, false);
  if (!pure || !body) return std::nullopt;
  return Pattern{.ordinary = call, .admitted = ops.size(), .helper = *helper,
                 .other_calls = {}, .normalizer = *pure, .body = *body,
                 .ordinary_begin = start, .admitted_begin = ops.size(),
                 .recall_second = start + 2,
                 .admitted_first = ops.size(), .admitted_second = ops.size(),
                 .register_arguments = register_arguments, .partitioned = false};
}

void execute_prologue(Evaluation& evaluation, const std::vector<IrOp>& ops,
                      const Pattern& p) {
  for (std::size_t i = p.ordinary_begin; i < p.ordinary; ++i) {
    if (i == p.ordinary_begin || i == p.ordinary_begin + 3)
      evaluation.call(p.normalizer);
    else
      evaluation.execute(ops[i]);
  }
}

bool prove_unrestricted_prologue(const std::vector<IrOp>& ops, const Pattern& p) {
  Evaluation before, after;
  execute_prologue(before, ops, p);
  before.call(p.body);
  after.synchronize_call();
  execute_prologue(after, ops, p);
  for (const auto& op : p.body) after.execute(op);
  after.synchronize_call();
  return same(before, after);
}

bool all_entries_are_calls(const std::vector<IrOp>& ops, const Index& index,
                           Pattern& p) {
  // No side entry into either replaced caller slice or the common body.
  std::size_t end = p.helper;
  while (end < ops.size() && ops[end].kind != IrKind::Return) ++end;
  if (end == ops.size()) return false;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    const auto& op = ops[i];
    const auto inspect = [&](const IrTarget& t) {
      const auto entry = target(t, index);
      if (!entry) return false;
      if ((*entry > p.ordinary_begin && *entry <= p.ordinary) ||
          (*entry > p.admitted_begin && *entry <= p.admitted) ||
          (*entry > p.helper && *entry <= end)) return false;
      if (*entry != p.helper) return true;
      if (call_target(op, index) != p.helper) return false;
      if (i != p.ordinary && i != p.admitted) p.other_calls.push_back(i);
      return true;
    };
    if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
         op.kind == IrKind::Loop || op.kind == IrKind::Call) && !inspect(op.target))
      return false;
    if (op.meta.indirect_flow_targets)
      for (const auto& t : *op.meta.indirect_flow_targets)
        if (!inspect(t)) return false;
  }
  return true;
}

bool prove_pair(const std::vector<IrOp>& ops, const Pattern& p,
                const OrdinaryNumericRange& first, const OrdinaryNumericRange& second,
                std::size_t maximum_pairs) {
  const auto finite = [](const auto& r) {
    return r.integral && r.lower >= 0 && r.upper <= 99999999 &&
           r.lower <= r.upper && r.upper - r.lower < 16;
  };
  if (!finite(first) || !finite(second)) return false;
  const auto pairs = static_cast<std::size_t>((first.upper - first.lower + 1) *
                                               (second.upper - second.lower + 1));
  if (pairs > maximum_pairs) return false;
  const auto& recall = ops[p.recall_second];
  const auto a = static_cast<std::size_t>(register_index(ops[p.admitted_first].register_name));
  const auto b = static_cast<std::size_t>(register_index(ops[p.admitted_second].register_name));

  for (std::int64_t u = first.lower; u <= first.upper; ++u) {
    if (!fixed_point(p.normalizer, u)) return false;
    for (std::int64_t v = second.lower; v <= second.upper; ++v) {
      if (a == b && u != v) continue;
      if (!fixed_point(p.normalizer, v)) return false;
      Evaluation before, after;
      before.state.registers[a] = *number(std::to_string(u));
      before.state.registers[b] = *number(std::to_string(v));
      after = before;
      for (std::size_t i = p.admitted_begin; i < p.admitted; ++i) before.execute(ops[i]);
      before.call(p.body);
      after.execute(ops[p.admitted_second]);
      after.execute(make_store(recall.register_name));
      after.execute(ops[p.admitted_first]);
      after.synchronize_call();
      execute_prologue(after, ops, p);
      for (const auto& op : p.body) after.execute(op);
      after.synchronize_call();
      if (!same(before, after)) return false;
    }
  }
  // Its unbounded caller has an independent exact-term proof; the finite
  // enumeration above never constrains the ordinary input.
  return prove_unrestricted_prologue(ops, p);
}

PassResult rewrite(const std::vector<IrOp>& ops, const Index& index,
                   const Pattern& p) {
  std::string body_label = "__common_processor_body";
  while (index.labels.contains(body_label)) body_label += '_';
  std::vector<IrOp> prefix(ops.begin() + static_cast<std::ptrdiff_t>(p.ordinary_begin),
                           ops.begin() + static_cast<std::ptrdiff_t>(p.ordinary));
  for (auto& op : prefix) op.meta.manual_interaction.reset();
  const std::set<std::size_t> others(p.other_calls.begin(), p.other_calls.end());
  PassResult result{.applied = 1};
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (i == p.ordinary_begin) {
      auto call = ops[p.ordinary];
      call.meta.manual_interaction = ops[i].meta.manual_interaction;
      result.ops.push_back(std::move(call));
    }
    if (i >= p.ordinary_begin && i <= p.ordinary) continue;
    if (!p.partitioned && i == p.admitted_begin) {
      result.ops.push_back(ops[p.admitted_second]);
      result.ops.push_back(make_store(ops[p.recall_second].register_name));
      result.ops.push_back(ops[p.admitted_first]);
    }
    if (!p.partitioned && i >= p.admitted_begin && i < p.admitted) continue;
    if (i == p.helper) {
      result.ops.insert(result.ops.end(), prefix.begin(), prefix.end());
      IrOp label;
      label.kind = IrKind::Label;
      label.name = body_label;
      result.ops.push_back(std::move(label));
    }
    auto op = ops[i];
    if (others.contains(i)) {
      op.kind = IrKind::Call;
      op.opcode = 0x53;
      op.target = body_label;
      op.register_name.clear();
      op.meta.mnemonic = opcode_by_code(op.opcode).name;
      op.meta.indirect_flow_targets.reset();
      op.meta.indirect_flow_formal_targets.reset();
      op.target_meta = {};
    }
    result.ops.push_back(std::move(op));
  }
  result.optimizations.push_back({p.partitioned ? "partitioned-common-processor" : "finite-domain-common-processor",
      std::string(p.partitioned ? "Sank one normalization prologue and retained the other callers unchanged at a separate body entry. " : "Factored a two-argument normalization prologue into a shared consumer. ") +
      std::string(p.partitioned ? "Proved the unrestricted moved prologue with exact symbolic terms; " : "Exhaustively proved the admitted integral domains with exact decimal arithmetic; ") +
      "compared every register, X/Y/Z/T, X1, X2 and possible arithmetic observation. "
      "Retained the divergent body entry and deferred selector/layout obligations. " +
      std::string(p.partitioned ?
          "Kept every other caller at its unchanged body entry." : p.register_arguments ?
          "Composed a two-register argument ABI with either proved preparation order." :
          "Composed the existing X-argument ABI.")});
  return result;
}

} // namespace

std::vector<PassResult> common_processor_factoring_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const CommonProcessorFactoringOptions& options) {
  std::vector<PassResult> results;
  if (!options.runtime_preloads || !options.maximum_candidates || !options.maximum_domain_pairs)
    return results;
  const auto index = index_ops(ops);
  if (!index.valid) return results;
  std::vector<Pattern> patterns;
  // Keep historical X-argument alternatives ahead of the new register ABI.
  // Neither discovery nor the proof depends on a consumer's source identity.
  for (bool register_arguments : {false, true}) {
    const std::size_t width = register_arguments ? 5 : 4;
    const std::size_t admitted_width = register_arguments ? 4 : 3;
    for (std::size_t c = width; c < ops.size(); ++c) {
      const auto base = projection_prologue(ops, index, c, register_arguments);
      if (!base) continue;
      const auto start = base->ordinary_begin;
      const auto helper = base->helper;
      for (std::size_t other = admitted_width; other < ops.size(); ++other) {
        const std::size_t admitted = other - admitted_width;
        if (other == c || call_target(ops[other], index) != std::optional{helper} ||
            !(other < start || c < admitted))
          continue;
        const auto is_transport = [&](std::size_t pos, IrKind kind) {
          return ops[pos].kind == kind && plain_metadata(ops[pos]) &&
                 ordinary_register(ops[pos]);
        };
        if (!is_transport(admitted, IrKind::Recall) ||
            !is_transport(admitted + 1, IrKind::Store) ||
            !is_transport(admitted + 2, IrKind::Recall))
          continue;
        bool reversed = false;
        if (register_arguments) {
          if (!is_transport(admitted + 3, IrKind::Store)) continue;
          const bool forward = register_index(ops[admitted + 1].register_name) == register_index(ops[start + 1].register_name) &&
              register_index(ops[admitted + 3].register_name) == register_index(ops[start + 4].register_name);
          reversed = register_index(ops[admitted + 1].register_name) == register_index(ops[start + 4].register_name) &&
              register_index(ops[admitted + 3].register_name) == register_index(ops[start + 1].register_name);
          if (!forward && !reversed) continue;
        } else if (register_index(ops[admitted + 1].register_name) != register_index(ops[start + 1].register_name)) {
          continue;
        }
        // Querying X after a recall must bound its original source word, not a
        // value supplied by a preceding destructive store in this prefix.
        // Otherwise a singleton range could hide an arbitrary old register.
        if (register_index(ops[admitted + 1].register_name) == register_index(ops[admitted + 2].register_name) &&
            register_index(ops[admitted].register_name) != register_index(ops[admitted + 1].register_name))
          continue;
        Pattern p = *base;
        p.admitted = other;
        p.admitted_begin = admitted;
        p.admitted_first = admitted + (reversed ? 2 : 0);
        p.admitted_second = admitted + (reversed ? 0 : 2);
        if (all_entries_are_calls(ops, index, p)) patterns.push_back(std::move(p));
      }
    }
  }
  if (patterns.empty()) return results;
  const auto items = lower_ir_to_machine(ops);
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  auto flow = build_post_layout_control_flow(items, control);
  if (!flow.proved) return results;
  const auto product = build_selector_consistent_execution_graph(items, flow);
  if (!product.proved) return results;
  const auto original_states = flow.execution_states;
  flow.execution_states.clear();
  for (const auto state : product.original_states)
    flow.execution_states.push_back(original_states.at(state));
  flow.execution_successors = product.successors;
  flow.execution_edges = product.execution_edges;
  const auto lift = analyze_post_layout_automatic_entry_lift(items, flow, options.maximum_proof_work);
  if (!lift) return results;
  std::vector<std::size_t> queries;
  for (const auto& p : patterns) {
    queries.push_back(index.item[p.admitted_first + 1]);
    queries.push_back(index.item[p.admitted_second + 1]);
  }
  const auto ranges = prove_post_layout_numeric_ranges(
      items, flow, *lift, queries, options.runtime_preloads, options.maximum_proof_work,
      NumericRangeInputPolicy::ValidatedRegisterTransport);
  if (ranges.exhausted) return results;
  for (const auto& p : patterns) {
    const auto a = ranges.x_before.find(index.item[p.admitted_first + 1]);
    const auto b = ranges.x_before.find(index.item[p.admitted_second + 1]);
    if (a == ranges.x_before.end() || b == ranges.x_before.end() ||
        !prove_pair(ops, p, a->second, b->second, options.maximum_domain_pairs))
      continue;
    auto candidate = rewrite(ops, index, p);
    const auto after = build_post_layout_control_flow(lower_ir_to_machine(candidate.ops), control);
    // The authoritative graph also proves the physical-01 continuation of a
    // startup empty return. The older symbolic walk rejects that valid entry
    // unconditionally and is not an additional safety proof here.
    if (!after.proved || after.maximum_observed_return_depth > control.maximum_return_depth)
      continue;
    results.push_back(std::move(candidate));
    if (results.size() >= options.maximum_candidates) break;
  }
  return results;
}


std::vector<PassResult> common_processor_partitioned_entry_candidates(
    const std::vector<IrOp>& ops, const PassContext& context,
    const CommonProcessorFactoringOptions& options) {
  std::vector<PassResult> results;
  if (!options.runtime_preloads || !options.maximum_candidates || !options.maximum_proof_work)
    return results;
  const auto index = index_ops(ops);
  if (!index.valid) return results;
  std::vector<Pattern> patterns;
  std::size_t remaining_work = options.maximum_proof_work;
  bool exhausted = false;
  for (bool register_arguments : {false, true}) {
    if (exhausted) break;
    const std::size_t width = register_arguments ? 5 : 4;
    for (std::size_t c = width; c < ops.size(); ++c) {
      auto p = projection_prologue(ops, index, c, register_arguments);
      if (!p) continue;
      p->partitioned = true;
      if (!all_entries_are_calls(ops, index, *p) || p->other_calls.empty()) continue;
      // Account for both symbolic executions, including all nested leaf
      // instructions and entry/return synchronizations. No partial proof escapes.
      const auto work = 2 * (2 * p->normalizer.size() + p->body.size() + width + 4);
      if (work > remaining_work) { exhausted = true; break; }
      remaining_work -= work;
      if (prove_unrestricted_prologue(ops, *p))
        patterns.push_back(std::move(*p));
    }
  }
  // Most programs have no such prologue; do not build an execution graph just
  // to find that out, or change the bounded compiler's timing on unrelated code.
  if (patterns.empty()) return results;
  PostLayoutControlFlowOptions control;
  control.empty_return_target = 1;
  control.address_space_model = address_space_model_for_feature_profile(
      effective_optimizer_feature_profile(context.options));
  const auto flow = build_post_layout_control_flow(lower_ir_to_machine(ops), control);
  if (!flow.proved) return results;
  for (const auto& p : patterns) {
    auto candidate = rewrite(ops, index, p);
    const auto final = build_post_layout_control_flow(lower_ir_to_machine(candidate.ops), control);
    if (!final.proved || final.maximum_observed_return_depth > control.maximum_return_depth)
      continue;
    results.push_back(std::move(candidate));
    if (results.size() >= options.maximum_candidates) break;
  }
  return results;
}

} // namespace mkpro::core::passes
