#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/detail/cell_provenance.hpp"

#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iterator>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {
namespace {


bool only_cell_provenance(const std::vector<CellRole>& roles) {
  return std::all_of(roles.begin(), roles.end(), detail::is_cell_origin_provenance);
}

constexpr int kRegisters = 15;
constexpr std::array<int, 4> kLoopOpcodes{0x5d, 0x5b, 0x58, 0x5a};
using Node = std::size_t;
using Nodes = std::set<Node>;
// Definition identities are dense and immutable during reaching and liveness analysis.
// Contiguous bit rows avoid a tree allocation per definition on every CFG
// iteration. Iteration remains sorted, matching the previous std::set order.
class DefinitionBits {
 public:
  struct const_iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = Node;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = Node;
    const DefinitionBits* owner = nullptr;
    std::size_t word = 0;
    std::uint64_t remaining = 0;
    void seek() {
      while (remaining == 0 && word < owner->words_.size()) {
        ++word;
        if (word < owner->words_.size()) remaining = owner->words_[word];
      }
    }
    Node operator*() const { return word * 64U + static_cast<Node>(std::countr_zero(remaining)); }
    const_iterator& operator++() { remaining &= remaining - 1U; seek(); return *this; }
    const_iterator operator++(int) { auto old = *this; ++*this; return old; }
    bool operator==(const const_iterator&) const = default;
  };
  const_iterator begin() const {
    const_iterator result{this, 0, words_.empty() ? 0 : words_.front()};
    result.seek(); return result;
  }
  const_iterator end() const { return {this, words_.size(), 0}; }
  bool insert(Node node) {
    if (words_.size() <= node / 64U) words_.resize(node / 64U + 1U);
    auto& word = words_[node / 64U];
    const auto mask = std::uint64_t{1} << (node % 64U);
    const bool changed = (word & mask) == 0;
    word |= mask; return changed;
  }
  void erase(Node node) {
    if (node / 64U < words_.size())
      words_[node / 64U] &= ~(std::uint64_t{1} << (node % 64U));
  }
  bool merge(const DefinitionBits& other) {
    if (words_.size() < other.words_.size()) words_.resize(other.words_.size());
    bool changed = false;
    for (std::size_t i = 0; i < other.words_.size(); ++i) {
      const auto united = words_[i] | other.words_[i];
      changed = changed || united != words_[i]; words_[i] = united;
    }
    return changed;
  }
  void intersect(const DefinitionBits& other) {
    const auto common = std::min(words_.size(), other.words_.size());
    for (std::size_t i = 0; i < common; ++i) words_[i] &= other.words_[i];
    words_.resize(common);
  }
  void subtract(const DefinitionBits& other) {
    const auto common = std::min(words_.size(), other.words_.size());
    for (std::size_t i = 0; i < common; ++i) words_[i] &= ~other.words_[i];
  }
  bool operator==(const DefinitionBits& other) const {
    const auto common = std::min(words_.size(), other.words_.size());
    for (std::size_t i = 0; i < common; ++i)
      if (words_[i] != other.words_[i]) return false;
    // Trailing allocated zero words are not part of a data-flow fact.
    for (std::size_t i = common; i < words_.size(); ++i)
      if (words_[i] != 0) return false;
    for (std::size_t i = common; i < other.words_.size(); ++i)
      if (other.words_[i] != 0) return false;
    return true;
  }
 private:
  std::vector<std::uint64_t> words_;
};
using Reaching = std::array<DefinitionBits, kRegisters>;

std::optional<int> counter_register(const IrOp& op) {
  if (op.kind != IrKind::Loop || op.counter.size() != 2 || op.counter[0] != 'L' ||
      op.counter[1] < '0' || op.counter[1] > '3')
    return std::nullopt;
  const int reg = op.counter[1] - '0';
  if (op.opcode != kLoopOpcodes[static_cast<std::size_t>(reg)])
    return std::nullopt;
  return reg;
}

bool append(Nodes& into, const DefinitionBits& from) {
  const auto before = into.size();
  into.insert(from.begin(), from.end());
  return before != into.size();
}

std::optional<int> physical(const std::string& name) {
  if (name.size() != 1)
    return std::nullopt;
  if (name[0] >= '0' && name[0] <= '9')
    return name[0] - '0';
  if (name[0] >= 'a' && name[0] <= 'e')
    return name[0] - 'a' + 10;
  return std::nullopt;
}

bool symbolic_flow(const IrOp& op) {
  if (op.kind == IrKind::OrphanAddress || op.target_meta.formal_opcode.has_value())
    return false;
  if ((op.kind == IrKind::Jump || op.kind == IrKind::CondJump ||
       op.kind == IrKind::Call || op.kind == IrKind::Loop) &&
      !std::holds_alternative<std::string>(op.target))
    return false;
  if (op.meta.indirect_flow_targets.has_value())
    for (const auto& target : *op.meta.indirect_flow_targets)
      if (!std::holds_alternative<std::string>(target))
        return false;
  return true;
}

struct Webs {
  std::vector<Node> parents;
  std::vector<int> colors;
  std::vector<bool> fixed;
  std::vector<std::set<int>> allowed;

  Node add(int color, bool anchored = false) {
    const Node id = parents.size();
    parents.push_back(id);
    colors.push_back(color);
    fixed.push_back(anchored);
    allowed.emplace_back();
    for (int reg = 0; reg < kRegisters; ++reg)
      allowed.back().insert(reg);
    return id;
  }

  Node root(Node node) {
    if (parents[node] != node)
      parents[node] = root(parents[node]);
    return parents[node];
  }

  Node join(Node a, Node b, int color) {
    a = root(a);
    b = root(b);
    if (a > b)
      std::swap(a, b);
    parents[b] = a;
    colors[a] = color;
    fixed[a] = fixed[a] || fixed[b];
    for (auto candidate = allowed[a].begin(); candidate != allowed[a].end(); ) {
      if (!allowed[b].contains(*candidate))
        candidate = allowed[a].erase(candidate);
      else
        ++candidate;
    }
    return a;
  }

  void same_register(const Nodes& nodes) {
    if (nodes.empty())
      return;
    Node first = root(*nodes.begin());
    for (const Node node : nodes)
      first = join(first, node, colors[first]);
  }
};

struct Copy {
  std::size_t store = 0;
  Node source = 0;
  Node destination = 0;
};

} // namespace

static PassResult process_register_webs(
    const std::vector<IrOp>& ops, const PassContext& context,
    const std::vector<RegisterWebRecoloringRequest>* recoloring_requests,
    const RegisterWebRecoloringConsumer* consume,
    const PostLayoutControlFlowOptions* deferred_control = nullptr,
    const std::map<std::string, int>* encoded_origins = nullptr,
    const std::vector<PreloadReport>* runtime_preloads = nullptr,
    bool allow_equal_preload_homes = false) {
  const bool recolor_only = recoloring_requests != nullptr;
  const bool closed_artifact_precision = runtime_preloads != nullptr;
  const auto movable_roles = [&](const std::vector<CellRole>& roles) {
    return closed_artifact_precision ? only_cell_provenance(roles) : roles.empty();
  };
  std::map<std::string, int> positions;
  int physical_address = 0;
  for (const auto& op : ops) {
    if (op.kind == IrKind::Label) positions.emplace(op.name, physical_address);
    physical_address += cells_per_op(op);
  }
  const bool closed_origins = closed_artifact_precision && encoded_origins &&
      std::all_of(encoded_origins->begin(), encoded_origins->end(), [&](const auto& binding) {
        return positions.contains(binding.first) && positions.at(binding.first) == binding.second;
      }) && std::all_of(ops.begin(), ops.end(), [&](const auto& op) {
        if (!op.meta.indirect_flow_targets) return true;
        return std::all_of(op.meta.indirect_flow_targets->begin(),
                           op.meta.indirect_flow_targets->end(), [&](const auto& target) {
          const auto* label = std::get_if<std::string>(&target);
          return label && encoded_origins->contains(*label);
        });
      });
  const auto symbolic_or_closed_formal = [&](const IrOp& op) {
    if (symbolic_flow(op)) return true;
    // Recoloring keeps physical geometry. Copy deletion cannot silently move
    // a fixed formal entry or its side-space discontinuity; those layouts must
    // first be reopened by an independently proved command-identity rewrite.
    if (!recolor_only || !closed_artifact_precision || !deferred_control || !closed_origins ||
        op.kind == IrKind::OrphanAddress) return false;
    auto ordinary = op;
    ordinary.target_meta.formal_opcode.reset();
    return symbolic_flow(ordinary);
  };
  const auto selector_view = [&](const std::vector<MachineItem>& machine,
                                 const AuthoritativePostLayoutControlFlow& flow) {
    return closed_origins ? build_selector_consistent_execution_graph(machine, flow) :
        build_deferred_selector_consistent_execution_graph(machine, flow, *encoded_origins).execution;
  };
  const bool trace = std::getenv("MKPRO_NATIVE_TRACE_REGISTER_WEBS") != nullptr;
  const auto unchanged = [&](const char* reason = "no admissible copy merge") {
    if (trace && context.options.coalesce_copies)
      std::cerr << "[register-web-copy-coalesce] skipped: " << reason << '\n';
    return PassResult{.ops = ops};
  };
  if (!context.options.coalesce_copies || ops.empty())
    return unchanged();
  bool has_copy = false;
  for (std::size_t i = 1; i < ops.size(); ++i)
    has_copy = has_copy || (ops[i - 1].kind == IrKind::Recall &&
                            ops[i].kind == IrKind::Store);
  if (!has_copy && !recolor_only)
    return unchanged("no adjacent recall/store");

  std::vector<RegisterEffects> effects;
  std::map<int, std::size_t> instructions;
  int address = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    const auto& op = ops[i];
    if (op.meta.raw || op.meta.logical_register_analysis || !symbolic_or_closed_formal(op)) {
      if (trace)
        std::cerr << "[register-web-copy-coalesce] opaque-index=" << i
                  << " opcode=" << op.opcode << '\n';
      return unchanged("opaque, logical or frozen operation");
    }
    if (!op.register_name.empty() && !physical(op.register_name).has_value())
      return unchanged("nonstandard register operand");
    if ((recolor_only || closed_artifact_precision) &&
        (op.kind == IrKind::Store || op.kind == IrKind::Recall)) {
      const auto reg = physical(op.register_name);
      if (!reg.has_value() ||
          op.opcode != (op.kind == IrKind::Store ? 0x40 : 0x60) + *reg)
        return unchanged("inconsistent direct register operand");
    }
    if (op.kind == IrKind::Loop && !counter_register(op).has_value())
      return unchanged("inconsistent loop counter operand");
    effects.push_back(register_effects(op));
    const auto& effect = effects.back();
    if (effect.uses_all_registers || effect.may_define_any_register)
      return unchanged("unknown register effects");
    for (const auto* names : {&effect.uses, &effect.must_defs, &effect.may_defs})
      for (const auto& name : *names)
        if (!physical(name).has_value())
          return unchanged();
    if (op.kind != IrKind::Label)
      instructions.emplace(address, i);
    address += cells_per_op(op);
  }
  const auto machine = lower_ir_to_machine(ops);
  auto flow = deferred_control ? build_post_layout_control_flow(machine, *deferred_control)
                               : build_post_layout_control_flow(machine);
  if (!flow.proved && !deferred_control) {
    PostLayoutControlFlowOptions options;
    options.empty_return_target = IrTarget{1};
    flow = build_post_layout_control_flow(machine, options);
  }
  if (!flow.proved || flow.execution_states.empty()) {
    if (trace && !flow.reasons.empty())
      std::cerr << "[register-web-copy-coalesce] CFG: " << flow.reasons.front() << '\n';
    return unchanged("exact execution graph unavailable");
  }

  if (encoded_origins) {
    const auto selector = selector_view(machine, flow);
    if (!selector.proved)
      return unchanged("deferred selector equations have no execution certificate");
    const auto states = flow.execution_states;
    flow.execution_states.clear();
    for (const auto original : selector.original_states)
      flow.execution_states.push_back(states.at(original));
    flow.execution_successors = selector.successors;
    flow.execution_edges = selector.execution_edges;
  }

  std::vector<std::size_t> sites;
  for (const auto& state : flow.execution_states) {
    const auto found = instructions.find(state.address);
    if (found == instructions.end())
      return unchanged();
    sites.push_back(found->second);
  }
  const std::set<std::size_t> reachable(sites.begin(), sites.end());
  const auto boundaries = encoded_origins
      ? prove_post_layout_input_boundaries(machine, flow) : PostLayoutInputBoundaryProof{};
  const auto transportable_reply = [&](std::size_t site) {
    if (!closed_artifact_precision || !boundaries.proved || site + 1U >= ops.size()) return false;
    const auto& read = ops[site];
    const auto& prompt = ops[site + 1U];
    return read.kind == IrKind::Recall && !has_rewrite_barrier(read) &&
        movable_roles(read.meta.roles) && movable_roles(read.target_meta.roles) &&
        prompt.kind == IrKind::Stop && prompt.meta.manual_interaction &&
        prompt.meta.manual_interaction->kind == ManualInteractionAnchorKind::PromptStop;
  };

  // With complete typed observations, comments are not a physical-register
  // contract. The entire def/use web and every later real read move together.
  // Explicit byte roles, raw/manual uses and public-bank exits stay anchored.
  const auto transportable_direct = [&](std::size_t site) {
    if(!closed_artifact_precision||!recolor_only||!boundaries.proved)return false;
    const auto& op=ops[site];const auto reg=physical(op.register_name);
    return reg&&(op.kind==IrKind::Store||op.kind==IrKind::Recall)&&
        op.opcode==(op.kind==IrKind::Store?0x40:0x60)+*reg&&
        !has_rewrite_barrier(op)&&movable_roles(op.meta.roles)&&movable_roles(op.target_meta.roles);
  };

  Webs webs;
  for (int reg = 0; reg < kRegisters; ++reg)
    webs.add(reg, true); // Preserve every possible setup-time entry value.

  if (closed_artifact_precision && allow_equal_preload_homes) {
    std::map<int,std::string> literals;
    std::set<int> ambiguous;
    for (const auto& preload : *runtime_preloads) {
      const auto reg = physical(preload.register_name);
      if (!reg) continue;
      if (literals.contains(*reg)) ambiguous.insert(*reg);
      if (!preload.setup_expression && !preload.setup_target_name && !preload.setup_expression_text &&
          !preload.counts_against_program && !preload.retunable_natural_fractional_prefix)
        literals.emplace(*reg,preload.value);
    }
    for (const auto& [reg,value] : literals) {
      if (ambiguous.contains(reg)) continue;
      std::set<int> homes;
      for (const auto& [other,word] : literals)
        if (!ambiguous.contains(other) && word==value) homes.insert(other);
      if (homes.size()<2U) continue;
      webs.fixed[static_cast<Node>(reg)] = false;
      webs.allowed[static_cast<Node>(reg)] = std::move(homes);
    }
  }

  std::vector<std::map<int, Node>> definitions(ops.size());
  std::vector<std::uint16_t> must_define(ops.size(), 0);
  for (std::size_t i = 0; i < ops.size(); ++i) {
    for (const auto* names : {&effects[i].must_defs, &effects[i].may_defs})
      for (const auto& name : *names) {
        const int reg = *physical(name);
        if (!definitions[i].contains(reg))
          definitions[i][reg] = webs.add(reg);
      }
  }
  for (std::size_t i = 0; i < ops.size(); ++i)
    for (const auto& name : effects[i].must_defs)
      must_define[i] |= std::uint16_t{1} << *physical(name);
  std::vector<Reaching> incoming(sites.size());
  std::vector<bool> queued(sites.size(), false);
  std::deque<std::size_t> pending{0};
  queued[0] = true;
  for (int reg = 0; reg < kRegisters; ++reg)
    incoming[0][static_cast<std::size_t>(reg)].insert(static_cast<Node>(reg));
  while (!pending.empty()) {
    const auto state = pending.front();
    pending.pop_front();
    queued[state] = false;
    const auto site = sites[state];
    // Transfer into successor rows directly: a must-definition kills only
    // this register, while a may-definition retains its incoming identities.
    for (const auto next : flow.execution_successors[state]) {
      bool changed = false;
      for (int reg = 0; reg < kRegisters; ++reg) {
        auto& destination = incoming[next][static_cast<std::size_t>(reg)];
        if ((must_define[site] & (std::uint16_t{1} << reg)) == 0)
          changed = destination.merge(incoming[state][static_cast<std::size_t>(reg)]) || changed;
        const auto definition = definitions[site].find(reg);
        if (definition != definitions[site].end()) {
          changed = destination.insert(definition->second) || changed;
        }
      }
      if (changed && !queued[next]) {
        pending.push_back(next);
        queued[next] = true;
      }
    }
  }

  // Prune proved-infeasible definition/use edges before unioning static
  // operands. Otherwise a dead hardware epoch permanently anchors a later
  // ordinary value, and no amount of graph coloring can undo that union.
  std::map<Node, std::pair<std::size_t, int>> definition_sites;
  for (std::size_t site = 0; site < definitions.size(); ++site)
    for (const auto& [reg, node] : definitions[site])
      definition_sites.emplace(node, std::pair{site, reg});
  std::map<std::size_t, Nodes> recall_sources, reply_sources, direct_sources;
  for (std::size_t state = 0; state < sites.size(); ++state) {
    const auto site = sites[state];
    const auto& op = ops[site];
    if (op.kind == IrKind::Recall && !has_rewrite_barrier(op)) {
      if (!is_display_focus_sensitive(op))
        append(recall_sources[site], incoming[state][static_cast<std::size_t>(*physical(op.register_name))]);
      else if (transportable_reply(site))
        append(reply_sources[site], incoming[state][static_cast<std::size_t>(*physical(op.register_name))]);
      else if (transportable_direct(site))
        append(direct_sources[site], incoming[state][static_cast<std::size_t>(*physical(op.register_name))]);
    }
  }
  std::set<GuardedDefinitionUseQuery> query_set;
  const auto add_queries = [&](const auto& demands) {
  for (const auto& [use, nodes] : demands) {
    if (nodes.size() < 2U)
      continue;
    for (const Node node : nodes) {
      const auto definition = definition_sites.find(node);
      if (definition == definition_sites.end() || query_set.size() >= 64U)
        continue;
      const auto site = definition->second.first;
      const auto& name = ops[use].register_name;
      if (effects[site].must_defs.contains(name))
        query_set.insert({site, use, name});
    }
  }
  };
  add_queries(recall_sources); // Keep the historical bounded proof portfolio intact.
  add_queries(reply_sources);
  add_queries(direct_sources);
  if (trace && recoloring_requests) {
    std::size_t potential = 0;
    for (const auto& [use, nodes] : recall_sources)
      if (nodes.size() > 1U) for (const Node node : nodes) {
        const auto d = definition_sites.find(node);
        if (d != definition_sites.end() &&
            effects[d->second.first].must_defs.contains(ops[use].register_name)) ++potential;
      }
    std::cerr << "[register-web-demand] potential=" << potential << " queried=" << query_set.size()
              << " requests=" << recoloring_requests->size() << '\n';
    for (const auto& request : *recoloring_requests) for (const auto& [a,b] : request)
      if (a < ops.size() && b < ops.size())
        std::cerr << "[register-web-demand] " << a << ':' << static_cast<int>(ops[a].kind)
                  << ':' << ops[a].register_name << " ~ " << b << ':'
                  << static_cast<int>(ops[b].kind) << ':' << ops[b].register_name << '\n';
  }
  LivenessOptions guarded_options;
  guarded_options.discover_toggle_projection_signs = closed_artifact_precision;
  for (const auto& [reg, value] : context.options.preloaded_constant_registers)
    guarded_options.equal_entry_value_classes.emplace(reg, "literal:" + value);
  if (runtime_preloads)
    for (const auto& preload : *runtime_preloads)
      if (!preload.setup_expression && !preload.setup_target_name && !preload.setup_expression_text)
        guarded_options.equal_entry_value_classes[preload.register_name] = "literal:" + preload.value;
  const auto excluded = prove_unreachable_definition_uses(
      ops, flow, {query_set.begin(), query_set.end()}, guarded_options);
  for (const auto& edge : excluded) {
    const int reg = *physical(edge.register_name);
    const Node node = definitions[edge.definition].at(reg);
    for (std::size_t state = 0; state < sites.size(); ++state)
      if (sites[state] == edge.use)
        incoming[state][static_cast<std::size_t>(reg)].erase(node);
  }
  if (trace && !excluded.empty())
    std::cerr << "[register-web-copy-coalesce] guarded exclusions=" << excluded.size() << '\n';

  // An emitted instruction has one physical operand, even when reached from
  // different callers. Join all definitions that can reach that static use.
  std::vector<bool> observes_final_bank(sites.size(), false);
  std::vector<std::map<int, Nodes>> uses(ops.size());
  std::vector<std::vector<std::size_t>> predecessors(sites.size());
  for (std::size_t state = 0; state < sites.size(); ++state) {
    const auto site = sites[state];
    for (const auto& name : effects[site].uses) {
      const int reg = *physical(name);
      append(uses[site][reg], incoming[state][static_cast<std::size_t>(reg)]);
      if (definitions[site].contains(reg))
        uses[site][reg].insert(definitions[site].at(reg));
    }
    for (const auto& name : effects[site].may_defs) {
      const int reg = *physical(name);
      append(uses[site][reg], incoming[state][static_cast<std::size_t>(reg)]);
      uses[site][reg].insert(definitions[site].at(reg));
    }
    // Late artifact rewrites have no source-level dead-output contract. A
    // terminal observation exposes the physical bank, even when the program
    // itself never recalls the final value. Preserve every reaching epoch.
    const auto& exit = ops[site];
    const bool private_terminal = closed_artifact_precision && exit.kind == IrKind::Stop && !exit.meta.raw &&
        exit.meta.stop_disposition == StopDisposition::Terminal &&
        std::find(exit.meta.roles.begin(), exit.meta.roles.end(), kTypedDisplayObservationRole) !=
            exit.meta.roles.end();
    observes_final_bank[state] = encoded_origins && flow.execution_successors[state].empty() && !private_terminal;
    if (observes_final_bank[state])
      for (int reg = 0; reg < kRegisters; ++reg)
        append(uses[site][reg], incoming[state][static_cast<std::size_t>(reg)]);
    for (const auto next : flow.execution_successors[state])
      predecessors[next].push_back(state);
  }
  for (const auto& by_register : uses)
    for (const auto& [reg, nodes] : by_register) {
      (void)reg;
      webs.same_register(nodes);
    }

  const auto movable_memory_cursor = [&](std::size_t site) {
    const auto& op = ops[site];
    const auto reg = physical(op.register_name);
    if (!closed_artifact_precision || !recolor_only || !reg || *reg > 3 ||
        (op.kind != IrKind::IndirectRecall && op.kind != IrKind::IndirectStore) ||
        op.opcode != (op.kind == IrKind::IndirectRecall ? 0xd0 : 0xb0) + *reg ||
        has_rewrite_barrier(op) || is_display_focus_sensitive(op) ||
        !movable_roles(op.meta.roles) || !movable_roles(op.target_meta.roles) ||
        !op.meta.indirect_memory_targets || op.meta.indirect_memory_targets->empty())
      return false;
    // Indexed banks stay physical and separate from every cursor home. R0's
    // exceptional positive-fraction decode reads R3, excluded by this fact.
    return std::all_of(op.meta.indirect_memory_targets->begin(),
        op.meta.indirect_memory_targets->end(), [](int target) { return target >= 4 && target < 15; });
  };
  const auto movable_counter = [&](std::size_t site) {
    const auto& op = ops[site];
    return counter_register(op).has_value() && !has_rewrite_barrier(op) &&
           !is_display_focus_sensitive(op) && op.meta.roles.empty() &&
           op.target_meta.roles.empty();
  };

  const auto transportable_input = [&](const IrOp& op) {
    const auto reg = physical(op.register_name);
    if (!boundaries.proved || op.kind != IrKind::Store || !reg ||
        op.opcode != 0x40 + *reg || !op.meta.manual_interaction ||
        op.meta.manual_interaction->kind != ManualInteractionAnchorKind::SingleStepCommand)
      return false;
    auto ordinary = op;
    ordinary.meta.manual_interaction.reset();
    return !has_rewrite_barrier(ordinary) && !is_display_focus_sensitive(ordinary) &&
        movable_roles(ordinary.meta.roles) && movable_roles(ordinary.target_meta.roles);
  };

  const auto anchored = [&](std::size_t site) {
    const auto& op = ops[site];
    if ((recolor_only || closed_artifact_precision) &&
        (!reachable.contains(site) || !movable_roles(op.meta.roles) ||
                        !movable_roles(op.target_meta.roles)))
      return true;
    if (movable_counter(site) || transportable_input(op) || transportable_reply(site) || transportable_direct(site))
      return false;
    return (op.kind != IrKind::Store && op.kind != IrKind::Recall) ||
           has_rewrite_barrier(op) || is_display_focus_sensitive(op);
  };
  for (std::size_t site = 0; site < ops.size(); ++site) {
    if (movable_counter(site)) {
      // FL0..FL3 differ only in the register they decrement. This is a
      // constraint on this definition web, not on every epoch of its name.
      for (const auto& [reg, nodes] : uses[site]) {
        (void)reg;
        for (const Node node : nodes)
          webs.allowed[webs.root(node)] = {0, 1, 2, 3};
      }
      for (const auto& [reg, node] : definitions[site]) {
        (void)reg;
        webs.allowed[webs.root(node)] = {0, 1, 2, 3};
      }
    }
    const bool memory_cursor = movable_memory_cursor(site);
    const int cursor = memory_cursor ? *physical(ops[site].register_name) : -1;
    if (memory_cursor) {
      const auto constrain = [&](Node node) {
        auto& allowed = webs.allowed[webs.root(node)];
        // Never move an R1..R3 positive fraction into R0's exception.
        const int first = cursor == 0 ? 0 : 1;
        for (auto color = allowed.begin(); color != allowed.end();)
          if (*color < first || *color > 3) color = allowed.erase(color);
          else ++color;
      };
      if (const auto found = uses[site].find(cursor); found != uses[site].end())
        for (const auto node : found->second) constrain(node);
      if (const auto found = definitions[site].find(cursor); found != definitions[site].end())
        constrain(found->second);
    }
    if (!anchored(site))
      continue;
    for (const auto& [reg, nodes] : uses[site]) {
      if (memory_cursor && reg == cursor) continue;
      (void)reg;
      for (const Node node : nodes)
        webs.fixed[webs.root(node)] = true;
    }
    for (const auto& [reg, node] : definitions[site]) {
      if (memory_cursor && reg == cursor) continue;
      (void)reg;
      webs.fixed[webs.root(node)] = true;
    }
  }

  std::vector<Copy> copies;
  for (std::size_t site = 1; site < ops.size(); ++site) {
    if (ops[site - 1].kind != IrKind::Recall || ops[site].kind != IrKind::Store ||
        anchored(site - 1) || anchored(site) || ops[site].meta.manual_interaction)
      continue;
    const int source = *physical(ops[site - 1].register_name);
    const int destination = *physical(ops[site].register_name);
    if (uses[site - 1][source].empty())
      continue;
    bool reached = false;
    bool exact_copy = true;
    for (std::size_t state = 0; state < sites.size(); ++state) {
      if (sites[state] != site)
        continue;
      reached = true;
      if (state == 0 || predecessors[state].empty())
        exact_copy = false;
      for (const auto prior : predecessors[state])
        exact_copy = exact_copy && sites[prior] == site - 1;
    }
    if (reached && exact_copy)
      copies.push_back({site, webs.root(*uses[site - 1][source].begin()),
                        webs.root(definitions[site].at(destination))});
  }
  if (copies.empty() && !recolor_only)
    return unchanged("no exact movable copy predecessor");

  // Backward demand is meaningful only for a definition that can reach this
  // exact execution state. In particular, a guarded use must not resurrect
  // an older loop iteration's web through a later physical-register overwrite.
  std::vector<DefinitionBits> available_in(sites.size()), available_out(sites.size());
  for (std::size_t state = 0; state < sites.size(); ++state) {
    const auto site = sites[state];
    for (const auto& by_register : incoming[state])
      for (const Node node : by_register) available_in[state].insert(webs.root(node));
    for (int reg = 0; reg < kRegisters; ++reg) {
      if ((must_define[site] & (std::uint16_t{1} << reg)) == 0)
        for (const Node node : incoming[state][static_cast<std::size_t>(reg)])
          available_out[state].insert(webs.root(node));
      const auto definition = definitions[site].find(reg);
      if (definition != definitions[site].end())
        available_out[state].insert(webs.root(definition->second));
    }
  }

  std::vector<DefinitionBits> live_in(sites.size()), live_out(sites.size());
  std::vector<DefinitionBits> read(sites.size()), must_def(sites.size()), all_def(sites.size());
  for (std::size_t state = 0; state < sites.size(); ++state) {
    const auto site = sites[state];
    for (const auto& name : effects[site].uses)
      for (const Node node : incoming[state][static_cast<std::size_t>(*physical(name))])
        read[state].insert(webs.root(node));
    if (observes_final_bank[state])
      for (const auto& by_register : incoming[state])
        for (const Node node : by_register) read[state].insert(webs.root(node));
    for (const auto& name : effects[site].must_defs)
      must_def[state].insert(webs.root(definitions[site].at(*physical(name))));
    for (const auto& [reg, node] : definitions[site]) {
      (void)reg;
      all_def[state].insert(webs.root(node));
    }
  }
  pending.clear();
  for (std::size_t state = 0; state < sites.size(); ++state) {
    pending.push_back(state);
    queued[state] = true;
  }
  while (!pending.empty()) {
    const auto state = pending.front();
    pending.pop_front();
    queued[state] = false;
    DefinitionBits out;
    for (const auto next : flow.execution_successors[state])
      out.merge(live_in[next]);
    out.intersect(available_out[state]);
    DefinitionBits in = out;
    in.subtract(must_def[state]);
    in.merge(read[state]);
    in.intersect(available_in[state]);
    live_out[state] = std::move(out);
    if (in == live_in[state])
      continue;
    live_in[state] = std::move(in);
    for (const auto prior : predecessors[state])
      if (!queued[prior]) {
        pending.push_back(prior);
        queued[prior] = true;
      }
  }

  std::map<std::size_t, Node> copy_source;
  for (const auto& copy : copies)
    copy_source[copy.store] = copy.source;
  std::set<GuardedOldWordQuery> old_word_demands;
  if (closed_artifact_precision) {
    std::set<std::pair<int,int>> requested_registers;
    if (recoloring_requests) for (const auto& request : *recoloring_requests) for (const auto& [left,right] : request) {
      if (left >= ops.size() || right >= ops.size()) continue;
      const auto l = physical(ops[left].register_name), r = physical(ops[right].register_name);
      if (l && r && *l != *r) requested_registers.emplace(std::min(*l,*r),std::max(*l,*r));
    }
    if (!recoloring_requests) for (const auto& copy : copies) {
      const int left = webs.colors[copy.source], right = webs.colors[copy.destination];
      if (left != right) requested_registers.emplace(std::min(left,right),std::max(left,right));
    }
    for (std::size_t state=0; state<sites.size() && old_word_demands.size()<64U; ++state)
      for (const Node defined : all_def[state]) for (const Node live : live_out[state]) {
        if(old_word_demands.size()>=64U)break;
        const int l=webs.colors[defined], r=webs.colors[live];
        if (l != r && requested_registers.contains({std::min(l,r),std::max(l,r)})) {
          old_word_demands.insert({sites[state],std::string(1,"0123456789abcde"[r])});
          if(old_word_demands.size()>=64U) break;
        }
      }
  }
  const auto old_words = analyze_dead_old_words(
      ops,flow,{old_word_demands.begin(),old_word_demands.end()},guarded_options);
  if (trace && !old_words.dead_words.empty())
    std::cerr<<"[register-web-copy-coalesce] dead old epochs="<<old_words.dead_words.size()<<'\n';
  std::set<std::pair<Node, Node>> interference;
  for (std::size_t state = 0; state < sites.size(); ++state)
    for (const Node defined : all_def[state])
      for (const Node live : live_out[state]) {
        // A copy creates an equal value. Other definitions still conflict
        // with a live source/destination and therefore prevent divergence.
        const auto copy = copy_source.find(sites[state]);
        const int live_register = webs.colors[live];
        const bool old_epoch_dead = old_words.dead_words.contains(
            {sites[state],std::string(1,"0123456789abcde"[live_register])});
        if (defined != live && !old_epoch_dead &&
            (copy == copy_source.end() || copy->second != live))
          interference.emplace(std::min(defined, live), std::max(defined, live));
      }

  std::set<std::size_t> removed;
  int joint_merges = 0;
  int recolored_assignments = 0;
  const auto joint_coloring = [&](Node a, Node b, std::size_t copy_store)
      -> std::optional<std::vector<int>> {
    if (webs.fixed[a] && webs.fixed[b] && webs.colors[a] != webs.colors[b])
      return std::nullopt;
    const auto name = [&](Node node) {
      node = webs.root(node);
      return "web:" + std::to_string(node == b ? a : node);
    };
    RegisterInterferenceGraph graph;
    PrecoloredRegisterAllocationOptions allocation;
    allocation.color_count = kRegisters;
    // Ordinary copy cleanup keeps its historical cheap policy. Explicit
    // multi-web equalities may need several hardware-domain moves together;
    // try the existing bounded checked list-color search after greedy fails.
    allocation.greedy_only = !recolor_only;
    allocation.maximum_recoloring_work = 4096;
    for (Node node = 0; node < webs.parents.size(); ++node) {
      if (webs.root(node) != node)
        continue;
      const std::string key = name(node);
      graph.neighbors.try_emplace(key);
      allocation.preferred_colors.try_emplace(key, webs.colors[node]);
      const auto [domain, inserted_domain] =
          allocation.allowed_colors.emplace(key, webs.allowed[node]);
      if (!inserted_domain) {
        for (auto color = domain->second.begin(); color != domain->second.end(); ) {
          if (!webs.allowed[node].contains(*color))
            color = domain->second.erase(color);
          else
            ++color;
        }
      }
      if (domain->second.empty())
        return std::nullopt;
      if (webs.fixed[node]) {
        const auto [entry, inserted] = allocation.fixed_colors.emplace(key, webs.colors[node]);
        if (!inserted && entry->second != webs.colors[node])
          return std::nullopt;
      }
    }
    allocation.preferred_colors[name(a)] = webs.fixed[b] ? webs.colors[b] : webs.colors[a];
    const auto add_edge = [&](const std::string& left, const std::string& right) {
      graph.neighbors[left].insert(right);
      graph.neighbors[right].insert(left);
    };
    for (const auto& [left, right] : interference) {
      const Node x = webs.root(left), y = webs.root(right);
      if (x == y)
        continue;
      if (name(x) == name(y))
        return std::nullopt;
      add_edge(name(x), name(y));
    }
    // Pool immutability is a whole-program constraint: later passes may add
    // reads which are absent from this execution graph. A removed copy does
    // not write the pool, but every surviving retargeted definition does.
    for (const auto& [reg, value] : context.options.preloaded_constant_registers) {
      (void)value;
      const auto color = physical(reg);
      if (!color.has_value())
        return std::nullopt;
      const std::string anchor = "pool:" + reg;
      graph.neighbors.try_emplace(anchor);
      allocation.fixed_colors[anchor] = *color;
      for (std::size_t site = 0; site < definitions.size(); ++site) {
        if (site == copy_store || removed.contains(site))
          continue;
        for (const auto& [original, node] : definitions[site])
          if (original != *color)
            add_edge(anchor, name(node));
      }
    }
    const auto colors = color_precolored_register_graph(graph, allocation);
    if (!colors.has_value())
      return std::nullopt;
    auto result = webs.colors;
    for (Node node = 0; node < webs.parents.size(); ++node) {
      if (webs.root(node) != node)
        continue;
      const int color = colors->at(name(node));
      if (color < 0 || color >= kRegisters ||
          !webs.allowed[node].contains(color) ||
          (webs.fixed[node] && color != webs.colors[node]))
        return std::nullopt;
      result[node] = color;
    }
    for (const auto& [left, right] : interference) {
      const Node x = webs.root(left), y = webs.root(right);
      if (x != y && result[x] == result[y])
        return std::nullopt;
    }
    return result;
  };
  const auto accepts = [&](Node a, Node b, int color, std::size_t copy_store) {
    if (!webs.allowed[a].contains(color) || !webs.allowed[b].contains(color))
      return false;
    if ((webs.fixed[a] && webs.colors[a] != color) ||
        (webs.fixed[b] && webs.colors[b] != color))
      return false;
    const std::string register_name(1, "0123456789abcde"[color]);
    if (context.options.preloaded_constant_registers.contains(register_name)) {
      // Later passes may introduce new recalls of this compiler-owned pool
      // entry. Existing CFG liveness alone cannot authorize making it mutable.
      // The disappearing copy is harmless; any other newly retargeted write
      // would invalidate the pool's whole-program constant contract.
      for (std::size_t site = 0; site < definitions.size(); ++site) {
        if (site == copy_store || removed.contains(site))
          continue;
        for (const auto& [original_register, node] : definitions[site]) {
          const Node root = webs.root(node);
          if ((root == a || root == b) && original_register != color)
            return false;
        }
      }
    }
    for (const auto& [left, right] : interference) {
      const Node x = webs.root(left);
      const Node y = webs.root(right);
      const bool x_inside = x == a || x == b;
      const bool y_inside = y == a || y == b;
      if (x != y && x_inside && y_inside)
        return false;
      if (x_inside && !y_inside && webs.colors[y] == color)
        return false;
      if (y_inside && !x_inside && webs.colors[x] == color)
        return false;
    }
    return true;
  };
  const auto emit_recolored = [&](int& relocated_counters) {
    std::vector<IrOp> result;
    result.reserve(ops.size() - removed.size());
    for (std::size_t site = 0; site < ops.size(); ++site) {
      if (removed.contains(site))
        continue;
      IrOp op = ops[site];
      if (op.kind == IrKind::Store || op.kind == IrKind::Recall) {
        const int reg = *physical(op.register_name);
        std::optional<Node> node;
        if (op.kind == IrKind::Store)
          node = definitions[site].at(reg);
        else if (!uses[site][reg].empty())
          node = *uses[site][reg].begin();
        if (node.has_value()) {
          const int color = webs.colors[webs.root(*node)];
          op.register_name = std::string(1, "0123456789abcde"[color]);
          op.opcode = (op.kind == IrKind::Store ? 0x40 : 0x60) + color;
          op.meta.mnemonic = opcode_by_code(op.opcode).name;
        }
      }
      if (movable_memory_cursor(site)) {
        const int original = *physical(op.register_name);
        const auto& sources = uses[site].at(original);
        if (!sources.empty()) {
          const int color = webs.colors[webs.root(*sources.begin())];
          if (color != original) {
            op.register_name = std::string(1, "0123456789abcde"[color]);
            op.opcode = (op.kind == IrKind::IndirectRecall ? 0xd0 : 0xb0) + color;
            op.meta.mnemonic = opcode_by_code(op.opcode).name;
            ++relocated_counters;
          }
        }
      }
      if (const auto original = counter_register(op)) {
        const Node node = definitions[site].at(*original);
        const int color = webs.colors[webs.root(node)];
        if (color != *original) {
          op.counter = "L" + std::to_string(color);
          op.opcode = kLoopOpcodes[static_cast<std::size_t>(color)];
          op.meta.mnemonic = opcode_by_code(op.opcode).name;
          ++relocated_counters;
        }
      }
      result.push_back(std::move(op));
    }
    return result;
  };

  if (recolor_only) {
    // Retain the same exact execution graph for all independent requests.
    // Joining operands expresses a common physical home, not value equality
    // and not permission to erase either defining instruction.
    const Webs original_webs = webs;
    const auto operand_web = [&](std::size_t site) -> std::optional<Node> {
      if (site >= ops.size() || !reachable.contains(site))
        return std::nullopt;
      const auto reg = ops[site].kind == IrKind::Loop
          ? counter_register(ops[site]) : physical(ops[site].register_name);
      if (!reg.has_value())
        return std::nullopt;
      if (ops[site].kind == IrKind::Store)
        return webs.root(definitions[site].at(*reg));
      if (ops[site].kind == IrKind::Recall || ops[site].kind == IrKind::IndirectRecall ||
          ops[site].kind == IrKind::IndirectStore || ops[site].kind == IrKind::Loop) {
        const auto found = uses[site].find(*reg);
        if (found != uses[site].end() && !found->second.empty())
          return webs.root(*found->second.begin());
      }
      return std::nullopt;
    };
    for (const auto& request : *recoloring_requests) {
      if (request.empty())
        continue;
      webs = original_webs;
      bool valid = true;
      for (const auto& [left, right] : request) {
        const auto a = operand_web(left), b = operand_web(right);
        if (!a.has_value() || !b.has_value() ||
            (webs.fixed[*a] && webs.fixed[*b] && webs.colors[*a] != webs.colors[*b])) {
          valid = false;
          break;
        }
        const int color = webs.fixed[*b] ? webs.colors[*b] : webs.colors[*a];
        const Node joined = webs.join(*a, *b, color);
        if (webs.allowed[joined].empty()) {
          valid = false;
          break;
        }
      }
      if (!valid)
        continue;
      // The coloring solver cannot see an edge after its endpoints have
      // become one node. Reject these contradictions before building it.
      for (const auto& [left, right] : interference)
        if (webs.root(left) == webs.root(right)) {
          if (trace) {
            std::cerr << "[register-web-demand] interference after equality";
            for (const auto& [a,b] : request) std::cerr << ' ' << a << '~' << b;
            std::cerr << '\n';
            const Node conflict = webs.root(left);
            for (const auto& [node, definition] : definition_sites)
              if (webs.root(node) == conflict)
                std::cerr << "[register-web-demand] definition " << definition.first
                          << " R" << definition.second << "\n";
            for (std::size_t site = 0; site < uses.size(); ++site)
              for (const auto& [reg,nodes] : uses[site]) for (const auto node : nodes)
                if (webs.root(node) == conflict)
                  std::cerr << "[register-web-demand] use " << site << " R" << reg << "\n";
          }
          valid = false;
          break;
        }
      if (!valid)
        continue;
      // All equalities are already joined. A self-pair adds no new binding;
      // ops.size() omits no definition from the immutable-pool proof.
      const Node anchor = webs.root(0);
      auto colors = joint_coloring(anchor, anchor, ops.size());
      if (!colors.has_value())
        continue;
      webs.colors = std::move(*colors);
      int relocated_counters = 0;
      auto candidate = emit_recolored(relocated_counters);
      if (deferred_control) {
        const auto changed_machine = lower_ir_to_machine(candidate);
        const auto changed_flow = build_post_layout_control_flow(changed_machine, *deferred_control);
        if (!changed_flow.proved ||
            !prove_post_layout_input_boundaries(changed_machine, changed_flow).proved ||
            !selector_view(changed_machine, changed_flow).proved)
          continue;
      }
      (*consume)(std::move(candidate));
    }
    return unchanged("region recolorings delivered without copy deletion");
  }

  for (const auto& copy : copies) {
    const Node source = webs.root(copy.source);
    const Node destination = webs.root(copy.destination);
    for (const int color : {webs.colors[source], webs.colors[destination]}) {
      if (!accepts(source, destination, color, copy.store))
        continue;
      webs.join(source, destination, color);
      removed.insert(copy.store);
      break;
    }
    if (removed.contains(copy.store))
      continue;
    // Local coalescing tries the two current colors. If a different epoch
    // occupies both choices, recolor the existing web graph as a whole. This
    // never splits a shared static operand or changes a hardware anchor.
    if (auto colors = joint_coloring(source, destination, copy.store)) {
      for (Node node = 0; node < webs.parents.size(); ++node)
        if (webs.root(node) == node && webs.colors[node] != colors->at(node))
          ++recolored_assignments;
      webs.colors = std::move(*colors);
      webs.join(source, destination, webs.colors[source]);
      removed.insert(copy.store);
      ++joint_merges;
    }
  }
  if (removed.empty())
    return unchanged();

  int relocated_counters = 0;
  std::vector<IrOp> result = emit_recolored(relocated_counters);
  if (deferred_control) {
    const auto after_machine = lower_ir_to_machine(result);
    const auto after_flow = build_post_layout_control_flow(after_machine, *deferred_control);
    if (!after_flow.proved ||
        !prove_post_layout_input_boundaries(after_machine, after_flow).proved ||
        !build_deferred_selector_consistent_execution_graph(
             after_machine, after_flow, *encoded_origins).execution.proved)
      return unchanged("coalesced callback/input obligations did not survive transport");
    // An empty return may name a physical command instead of a label. Do not
    // move or delete that command merely because the rebuilt graph is valid.
    if (flow.empty_return_target) {
      const int before_target = flow.empty_return_target->address;
      int old_address = 0, new_address = 0;
      bool preserved = false;
      for (std::size_t site = 0; site < ops.size(); ++site) {
        if (ops[site].kind != IrKind::Label && old_address == before_target) {
          preserved = !removed.contains(site) && after_flow.empty_return_target &&
              after_flow.empty_return_target->address == new_address;
          break;
        }
        old_address += cells_per_op(ops[site]);
        if (!removed.contains(site)) new_address += cells_per_op(ops[site]);
      }
      if (!preserved) return unchanged("empty-return command identity would move");
    }
  }
  if (trace)
    std::cerr << "[register-web-copy-coalesce] removed=" << removed.size()
              << " definitions=" << webs.parents.size()
              << " execution-states=" << sites.size() << '\n';
  std::vector<AppliedOptimization> optimizations{{"register-web-copy-coalesce",
      "Removed " + std::to_string(removed.size()) +
      " copy store(s) by coalescing exact reaching-definition webs; preserved "
      "entry values, hardware-sensitive epochs and matched caller lifetimes."}};
  if (encoded_origins)
    optimizations.push_back({"deferred-selector-web-coalescing",
        "Coalesced available definition webs on the selector-consistent execution graph; "
        "transported typed input stores without deleting or reordering a manual phase. "
        "Re-proved callback/input boundaries and empty-return identity; physical selector "
        "rebinding and final layout remain mandatory closure obligations."});
  if (!excluded.empty())
    optimizations.push_back({"guarded-definition-webs",
        "Excluded " + std::to_string(excluded.size()) +
        " infeasible definition/use edge(s) before web union. Guarded demand proofs "
        "preserve all other reads, definitions, caller contexts and hardware anchors; "
        "ordinary interference/coloring independently proves the resulting copy removal."});
  if (joint_merges > 0)
    optimizations.push_back({"register-web-joint-coloring",
        "Removed " + std::to_string(joint_merges) +
        " additional copy store(s) using " + std::to_string(recolored_assignments) +
        " web assignment change(s) from a joint, precolored interference graph. "
        "Deterministic checked coloring witnesses preserve setup, hardware and constant-pool anchors; "
        "failed coloring keeps the incumbent without exhaustive search."});
  if (relocated_counters > 0)
    optimizations.push_back({"register-web-counter-role-coalescing",
        "Retargeted " + std::to_string(relocated_counters) +
        " FL/memory-cursor instruction(s) within their proved R0..R3 domains. "
        "Web domains preserve live source values, entry/setup ownership, shared "
        "operands and manual/hardware anchors without spills or Rf."});
  return {.ops = std::move(result), .applied = static_cast<int>(removed.size()),
          .optimizations = std::move(optimizations)};
}

PassResult register_web_copy_coalesce(const std::vector<IrOp>& ops,
                                      const PassContext& context) {
  return process_register_webs(ops, context, nullptr, nullptr);
}

void for_each_register_web_recoloring(
    const std::vector<IrOp>& ops, const PassContext& context,
    const std::vector<RegisterWebRecoloringRequest>& requests,
    const RegisterWebRecoloringConsumer& consume) {
  if (!requests.empty() && consume)
    (void)process_register_webs(ops, context, &requests, &consume);
}

PassResult deferred_register_web_copy_coalesce(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control,
    const std::map<std::string, int>& encoded_origins) {
  return process_register_webs(ops, context, nullptr, nullptr, &control, &encoded_origins);
}

void for_each_deferred_register_web_recoloring(const std::vector<IrOp>& ops, const PassContext& context,
    const std::vector<RegisterWebRecoloringRequest>& requests,
    const PostLayoutControlFlowOptions& control,
    const std::map<std::string, int>& encoded_origins,
    const RegisterWebRecoloringConsumer& consume,
    const std::vector<PreloadReport>* runtime_preloads,
    bool allow_equal_preload_homes) {
  if (!requests.empty() && consume)
    (void)process_register_webs(ops, context, &requests, &consume, &control, &encoded_origins, runtime_preloads, allow_equal_preload_homes);
}

PassResult deferred_register_web_copy_coalesce(
    const std::vector<IrOp>& ops, const PassContext& context,
    const PostLayoutControlFlowOptions& control,
    const std::map<std::string, int>& encoded_origins,
    const std::vector<PreloadReport>& runtime_preloads) {
  return process_register_webs(ops,context,nullptr,nullptr,&control,&encoded_origins,
      &runtime_preloads,true);
}

IrPass register_web_copy_coalesce_pass() {
  return {.name = "register-web-copy-coalesce", .run = register_web_copy_coalesce,
          .layout_safe = false};
}

} // namespace mkpro::core::passes

