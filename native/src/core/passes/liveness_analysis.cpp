#include "mkpro/core/passes/liveness_analysis.hpp"

#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
#include "mkpro/core/passes/cfg.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/stable_register_value_flow.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace mkpro::core::passes {

namespace {

void insert_if_named(RegisterValueSet& registers, const std::string& register_name) {
  if (!register_name.empty())
    registers.insert(register_name);
}

void add_indirect_selector_definition(RegisterEffects& effects, const IrOp& op) {
  if (op.register_name.empty())
    return;
  // Symbolic analysis cannot derive the selector class from register_name, so
  // it uses the lowered opcode. Ordinary IR tests and hand-built passes may
  // carry only a base opcode; retain their physical-name contract.
  if (!op.meta.logical_register_analysis) {
    try {
      if (!core::is_stable_indirect_selector(op.register_name))
        effects.must_defs.insert(op.register_name);
    } catch (const std::exception&) {
    }
    return;
  }
  int selector = op.opcode & 0x0f;
  // The xF aliases are the R0 pre-decrement forms on the stock machine.
  if (selector == 0x0f)
    selector = 0;
  if (selector >= 0 && selector <= 6)
    effects.must_defs.insert(op.register_name);
}

RegisterValueSet physical_register_universe() {
  RegisterValueSet registers;
  for (int index = 0; index <= 9; ++index)
    registers.insert(std::to_string(index));
  for (char name = 'a'; name <= 'e'; ++name)
    registers.insert(std::string(1, name));
  return registers;
}

RegisterValueSet register_universe(const std::vector<IrOp>& ops,
                                   const std::vector<RegisterEffects>& effects,
                                   bool include_physical_register_universe) {
  RegisterValueSet registers =
      include_physical_register_universe ? physical_register_universe() : RegisterValueSet{};
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const IrOp& op = ops.at(index);
    switch (op.kind) {
    case IrKind::Store:
    case IrKind::Recall:
    case IrKind::IndirectStore:
    case IrKind::IndirectRecall:
    case IrKind::IndirectJump:
    case IrKind::IndirectCall:
    case IrKind::IndirectCondJump:
      insert_if_named(registers, op.register_name);
      break;
    case IrKind::Loop:
      insert_if_named(registers,
                      op.meta.logical_register_analysis && op.meta.logical_register_name.has_value()
                          ? *op.meta.logical_register_name
                          : loop_counter_register(op.counter));
      break;
    case IrKind::Label:
    case IrKind::Jump:
    case IrKind::CondJump:
    case IrKind::Call:
    case IrKind::Return:
    case IrKind::Stop:
    case IrKind::Plain:
    case IrKind::OrphanAddress:
      break;
    }
    registers.insert(effects.at(index).uses.begin(), effects.at(index).uses.end());
    registers.insert(effects.at(index).must_defs.begin(), effects.at(index).must_defs.end());
    registers.insert(effects.at(index).may_defs.begin(), effects.at(index).may_defs.end());
  }
  return registers;
}

bool sets_equal(const RegisterValueSet& left, const RegisterValueSet& right) {
  return left == right;
}

void add_interference(RegisterInterferenceGraph& graph, const std::string& left,
                      const std::string& right) {
  if (left.empty() || right.empty() || left == right)
    return;
  graph.neighbors[left].insert(right);
  graph.neighbors[right].insert(left);
}

void add_clique(RegisterInterferenceGraph& graph, const RegisterValueSet& registers) {
  for (auto left = registers.begin(); left != registers.end(); ++left) {
    graph.neighbors.try_emplace(*left);
    for (auto right = std::next(left); right != registers.end(); ++right)
      add_interference(graph, *left, *right);
  }
}

// A demand proof, not a replacement for conservative liveness. Cutting a
// queried value's definitions restricts the graph to paths that could still
// observe its OLD value. On that graph a conditional select can retain its
// previous input, exposing a constant guard that rules out the remaining read.
// No IR instruction, stack effect or condition is rewritten here.
struct GuardedLifetimeProof {
  std::set<std::pair<std::string, std::string>> disjoint_pairs;
  std::set<GuardedDefinitionUseQuery> excluded_uses;
  std::size_t work = 0;
  bool exhausted = false;
};

GuardedLifetimeProof guarded_register_lifetimes(
    const std::vector<IrOp>& ops, const std::vector<RegisterEffects>& effects,
    const std::vector<std::size_t>& instructions,
    const std::vector<std::vector<std::size_t>>& successors,
    const std::vector<std::vector<std::size_t>>& predecessors,
    const std::vector<std::size_t>& roots, const LivenessOptions& options,
    const std::vector<GuardedDefinitionUseQuery>& queries = {},
    std::size_t maximum_work = 400000U,
    const std::map<std::size_t,int>* exact_signs = nullptr) {
  using Id = std::size_t;
  using Pair = std::pair<std::string, std::string>;
  GuardedLifetimeProof proved;
  if ((options.equal_entry_value_classes.size() < 2U && queries.empty()) || instructions.empty() ||
      std::any_of(effects.begin(), effects.end(), [](const RegisterEffects& effect) {
        return effect.uses_all_registers || effect.may_define_any_register;
      }))
    return proved;

  struct SelectWindow {
    Id store = 0;
    Id branch = 0;
    Id zero = 0;
    Id nonzero = 0;
    std::string reg;
  };
  std::vector<SelectWindow> windows;
  RegisterValueSet guards;
  const auto op_at = [&](Id id) -> const IrOp& { return ops.at(instructions.at(id)); };
  // Branch encoding is not a predicate fact. Indirect conditions use the
  // same zero/sign tests; their selector effects still come from the original
  // IR and their exact alternatives still come from the caller's graph.
  const auto condition_code = [](const IrOp& op) {
    if (op.kind == IrKind::CondJump) return op.opcode;
    if (op.kind == IrKind::IndirectCondJump) {
      switch (op.opcode & 0xf0) {
      case 0x70: return 0x57;
      case 0x90: return 0x59;
      case 0xc0: return 0x5c;
      case 0xe0: return 0x5e;
      }
    }
    return -1;
  };
  const auto fallthrough_edge = [&](Id from, Id to) {
    // The IR liveness graph includes labels; the authoritative physical graph
    // skips them. Both describe the same fallthrough command.
    for (Id site = instructions.at(from) + 1U; site < ops.size(); ++site) {
      if (site == instructions.at(to))
        return true;
      if (ops.at(site).kind != IrKind::Label)
        break;
    }
    return false;
  };
  const auto unique_next = [&](Id id) -> std::optional<Id> {
    for (int labels = 0; labels < 16; ++labels) {
      if (successors.at(id).size() != 1U)
        return std::nullopt;
      id = successors.at(id).front();
      if (predecessors.at(id).size() != 1U)
        return std::nullopt;
      if (op_at(id).kind != IrKind::Label)
        return id;
    }
    return std::nullopt;
  };
  for (Id begin = 0; begin < instructions.size(); ++begin) {
    const IrOp& input = op_at(begin);
    if (input.kind != IrKind::Recall || input.register_name.empty() ||
        input.meta.manual_interaction.has_value())
      continue;
    const auto select = unique_next(begin);
    const auto write = select.has_value() ? unique_next(*select) : std::nullopt;
    const auto difference = write.has_value() ? unique_next(*write) : std::nullopt;
    const auto branch = difference.has_value() ? unique_next(*difference) : std::nullopt;
    if (!branch.has_value() || op_at(*select).meta.manual_interaction.has_value() ||
        op_at(*write).meta.manual_interaction.has_value() ||
        op_at(*difference).meta.manual_interaction.has_value() ||
        op_at(*branch).meta.manual_interaction.has_value() || op_at(*select).kind != IrKind::Plain ||
        op_at(*select).opcode != 0x36 || op_at(*write).kind != IrKind::Store ||
        op_at(*write).register_name != input.register_name ||
        op_at(*difference).kind != IrKind::Plain || op_at(*difference).opcode != 0x11 ||
        (condition_code(op_at(*branch)) != 0x5e && condition_code(op_at(*branch)) != 0x57) ||
        successors.at(*branch).size() != 2U)
      continue;
    const auto& edges = successors.at(*branch);
    const auto fallthrough = std::find_if(edges.begin(), edges.end(), [&](Id to) {
      return fallthrough_edge(*branch, to);
    });
    if (fallthrough == edges.end() ||
        std::count_if(edges.begin(), edges.end(), [&](Id to) {
          return fallthrough_edge(*branch, to);
        }) != 1)
      continue;
    const Id taken = edges.front() == *fallthrough ? edges.back() : edges.front();
    const bool equality = condition_code(op_at(*branch)) == 0x5e;
    windows.push_back({*write, *branch, equality ? *fallthrough : taken,
                       equality ? taken : *fallthrough, input.register_name});
    guards.insert(input.register_name);
  }
  if (windows.empty()) {
    if (std::getenv("MKPRO_NATIVE_TRACE_GUARDED_LIFETIMES") != nullptr)
      std::cerr << "[guarded-register-lifetime] no select window, ops=" << ops.size() << '\n';
    return proved;
  }

  std::vector<Pair> pairs;
  for (auto left = options.equal_entry_value_classes.begin();
       queries.empty() && left != options.equal_entry_value_classes.end(); ++left) {
    if (!left->second.starts_with("literal:"))
      continue;
    for (auto right = std::next(left); right != options.equal_entry_value_classes.end(); ++right) {
      if (left->second != right->second)
        continue;
      bool eligible = true;
      bool writes_left = false;
      bool writes_right = false;
      for (Id site = 0; site < ops.size(); ++site) {
        const auto& effect = effects.at(site);
        const bool touches = effect.uses.contains(left->first) || effect.uses.contains(right->first) ||
            effect.must_defs.contains(left->first) || effect.must_defs.contains(right->first) ||
            effect.may_defs.contains(left->first) || effect.may_defs.contains(right->first);
        if (touches && ops.at(site).kind != IrKind::Recall && ops.at(site).kind != IrKind::Store) {
          eligible = false;
          break;
        }
        writes_left = writes_left || effect.must_defs.contains(left->first);
        writes_right = writes_right || effect.must_defs.contains(right->first);
      }
      if (eligible && writes_left && writes_right)
        pairs.emplace_back(left->first, right->first);
    }
  }
  if (pairs.empty() && queries.empty())
    return proved;

  enum class Entry { Unknown, Closed, Digits };
  struct Facts {
    std::map<std::string, int> memory;
    std::optional<int> x;
    Entry entry = Entry::Unknown;
    bool operator==(const Facts&) const = default;
  };
  const auto small_integer = [](const std::string& value) -> std::optional<int> {
    if (!value.starts_with("literal:"))
      return std::nullopt;
    const char* first = value.data() + 8;
    const char* last = value.data() + value.size();
    int number = 0;
    const auto parsed = std::from_chars(first, last, number);
    if (parsed.ec != std::errc{} || parsed.ptr != last || number < -1024 || number > 1024)
      return std::nullopt;
    return number;
  };
  const auto transfer = [&](Facts state, Id id, bool retain_selector, bool global_path) {
    const IrOp& op = op_at(id);
    const RegisterEffects& effect = effects.at(instructions.at(id));
    const auto old_memory = state.memory;
    for (const std::string& reg : effect.must_defs)
      state.memory.erase(reg);
    for (const std::string& reg : effect.may_defs)
      state.memory.erase(reg);
    if (op.meta.manual_interaction.has_value()) {
      state.x.reset();
      state.entry = Entry::Unknown;
    }
    if (op.kind == IrKind::Store) {
      if (guards.contains(op.register_name)) {
        if (retain_selector) {
          if (const auto old = old_memory.find(op.register_name); old != old_memory.end())
            state.memory[op.register_name] = old->second;
        } else if (state.x.has_value()) {
          state.memory[op.register_name] = *state.x;
        }
      }
      state.entry = Entry::Closed;
    } else if (op.kind == IrKind::Recall) {
      const auto found = state.memory.find(op.register_name);
      state.x = found == state.memory.end() ? std::nullopt : std::optional<int>{found->second};
      state.entry = Entry::Closed;
    } else if (op.kind == IrKind::IndirectRecall) {
      state.x.reset();
      state.entry = Entry::Closed;
    } else if (op.kind == IrKind::IndirectStore) {
      // Even a singleton write is conservatively forgotten, not inferred from
      // X: this proof never needs an indirect write to establish a constant.
      state.entry = Entry::Closed;
    } else if (op.kind == IrKind::Stop) {
      state.x.reset();
      state.entry = Entry::Unknown;
    } else if (op.kind == IrKind::Plain) {
      if (op.opcode >= 0 && op.opcode <= 9) {
        if (state.entry == Entry::Closed)
          state.x = op.opcode;
        else if (state.entry == Entry::Digits && state.x.has_value() && *state.x >= 0 &&
                 *state.x * 10 + op.opcode <= 1024)
          state.x = *state.x * 10 + op.opcode;
        else
          state.x.reset();
        state.entry = Entry::Digits;
      } else if (op.opcode == 0x0b) {
        if (state.x.has_value())
          state.x = -*state.x;
        // The numeric value is known, but sign entry is not a proof that a
        // following digit starts a new mantissa rather than continuing input.
        state.entry = Entry::Unknown;
      } else if (op.opcode == 0x32 && global_path && exact_signs && exact_signs->contains(id)) {
        state.x=exact_signs->at(id);
        state.entry=Entry::Closed;
      } else if (op.opcode == 0x0d) {
        state.x = 0;
        state.entry = Entry::Closed;
      } else if (op.opcode == 0x0e) {
        state.entry = Entry::Closed;
      } else if (op.opcode == 0x54) {
        if (state.entry != Entry::Closed)
          state.entry = Entry::Unknown;
      } else {
        state.x.reset();
        state.entry = op.opcode >= 0x10 && op.opcode <= 0x3b ? Entry::Closed : Entry::Unknown;
      }
    } else if (op.kind != IrKind::Label && state.entry != Entry::Closed) {
      // Calls/returns preserve X, but an open keyboard entry is not assumed
      // to survive a flow instruction. A closed entry cannot become open.
      state.entry = Entry::Unknown;
    }
    return state;
  };
  const auto permits = [&](Id from, Id to, const Facts& input) {
    const IrOp& op = op_at(from);
    if (condition_code(op) < 0 || op.meta.manual_interaction.has_value() ||
        !input.x.has_value() || successors.at(from).size() != 2U)
      return true;
    std::optional<bool> predicate;
    switch (condition_code(op)) {
      case 0x5e: predicate = *input.x == 0; break;
      case 0x57: predicate = *input.x != 0; break;
      case 0x5c: predicate = *input.x < 0; break;
      case 0x59: predicate = *input.x >= 0; break;
      default: return true;
    }
    const bool has_fallthrough = std::count_if(successors.at(from).begin(), successors.at(from).end(),
        [&](Id edge) { return fallthrough_edge(from, edge); }) == 1;
    return !has_fallthrough ||
        (fallthrough_edge(from, to) == *predicate);
  };
  using StateTable = std::vector<std::optional<Facts>>;
  using Seed = std::pair<Id, Facts>;
  bool& exhausted = proved.exhausted;
  const auto propagate = [&](const std::vector<Seed>& seeds, const std::vector<bool>* relevant,
                             const RegisterValueSet* payload, const std::set<Id>& retain,
                             std::optional<Id> observed_use = std::nullopt)
      -> std::optional<StateTable> {
    StateTable states(instructions.size());
    std::deque<Id> work;
    std::vector<bool> queued(instructions.size(), false);
    const auto merge = [&](Id id, const Facts& incoming) {
      if (relevant != nullptr && !relevant->at(id))
        return;
      Facts next = incoming;
      if (states.at(id).has_value()) {
        next = *states.at(id);
        for (auto item = next.memory.begin(); item != next.memory.end();) {
          const auto other = incoming.memory.find(item->first);
          if (other == incoming.memory.end() || other->second != item->second)
            item = next.memory.erase(item);
          else
            ++item;
        }
        if (next.x != incoming.x)
          next.x.reset();
        if (next.entry != incoming.entry)
          next.entry = Entry::Unknown;
        if (next == *states.at(id))
          return;
      }
      states.at(id) = std::move(next);
      if (!queued.at(id)) {
        queued.at(id) = true;
        work.push_back(id);
      }
    };
    for (const auto& [id, facts] : seeds)
      merge(id, facts);
    while (!work.empty()) {
      if (proved.work >= maximum_work) {
        exhausted = true;
        return std::nullopt;
      }
      ++proved.work;
      const Id id = work.front();
      work.pop_front();
      queued.at(id) = false;
      const auto& effect = effects.at(instructions.at(id));
      if (payload != nullptr) {
        const std::string& name = *payload->begin();
        if (effect.uses.contains(name) &&
            (!observed_use.has_value() || instructions.at(id) == *observed_use)) {
          if (std::getenv("MKPRO_NATIVE_TRACE_GUARDED_LIFETIME_DETAILS") != nullptr) {
            std::cerr << "[guarded-register-lifetime] old read " << name << " at "
                      << instructions.at(id) << " facts:";
            for (const auto& [reg, value] : states.at(id)->memory)
              std::cerr << ' ' << reg << '=' << value;
            std::cerr << '\n';
          }
          return std::nullopt;
        }
        if (effect.must_defs.contains(name))
          continue;
      }
      const Facts after = transfer(*states.at(id), id, retain.contains(id), payload == nullptr);
      for (const Id to : successors.at(id))
        if (permits(id, to, *states.at(id)))
          merge(to, after);
    }
    return states;
  };

  Facts entry;
  if (std::getenv("MKPRO_NATIVE_TRACE_GUARDED_LIFETIME_DETAILS") != nullptr) {
    for (std::size_t i = 0; i < ops.size(); ++i) {
      const auto& op = ops.at(i);
      std::cerr << "[guarded-register-lifetime] ir " << i << " kind="
                << static_cast<int>(op.kind) << " opcode=" << op.opcode
                << " reg=" << op.register_name
                << " manual=" << op.meta.manual_interaction.has_value() << '\n';
    }
  }
  for (const auto& [reg, value] : options.equal_entry_value_classes) {
    if (std::getenv("MKPRO_NATIVE_TRACE_GUARDED_LIFETIME_DETAILS") != nullptr)
      std::cerr << "[guarded-register-lifetime] setup " << reg << '=' << value << '\n';
    if (const auto number = small_integer(value))
      entry.memory[reg] = *number;
  }
  std::vector<Seed> seeds;
  Facts invariant = entry;
  for (const auto& effect : effects) {
    for (const auto& reg : effect.must_defs)
      invariant.memory.erase(reg);
    for (const auto& reg : effect.may_defs)
      invariant.memory.erase(reg);
  }
  for (const Id root : roots)
    if (root == 0U || !options.closed_program_entry)
      seeds.emplace_back(root, root == 0U ? entry : invariant);
  const auto global = propagate(seeds, nullptr, nullptr, {});
  if (!global.has_value())
    return proved;

  struct Slice {
    std::vector<bool> relevant;
    std::set<Id> retain;
  };
  using SliceKey = std::pair<std::string, std::optional<Id>>;
  std::map<SliceKey, Slice> slices;
  const auto get_slice = [&](const std::string& payload,
                             std::optional<Id> observed_use = std::nullopt) -> const Slice& {
    const SliceKey key{payload, observed_use};
    if (const auto found = slices.find(key); found != slices.end())
      return found->second;
    Slice slice{std::vector<bool>(instructions.size(), false), {}};
    std::deque<Id> work;
    for (Id id = 0; id < instructions.size(); ++id)
      if (effects.at(instructions.at(id)).uses.contains(payload) &&
          (!observed_use.has_value() || instructions.at(id) == *observed_use)) {
        slice.relevant.at(id) = true;
        work.push_back(id);
      }
    while (!work.empty()) {
      const Id id = work.front();
      work.pop_front();
      for (const Id from : predecessors.at(id))
        if (!slice.relevant.at(from) &&
            !effects.at(instructions.at(from)).must_defs.contains(payload)) {
          slice.relevant.at(from) = true;
          work.push_back(from);
        }
    }
    for (const SelectWindow& window : windows)
      if (slice.relevant.at(window.branch) && !slice.relevant.at(window.zero) &&
          slice.relevant.at(window.nonzero))
        slice.retain.insert(window.store);
    return slices.emplace(key, std::move(slice)).first->second;
  };
  const auto dead_after_other_writes = [&](const std::string& payload, const std::string& other) {
    const Slice& slice = get_slice(payload);
    std::vector<Seed> starts;
    for (Id id = 0; id < instructions.size(); ++id) {
      if (!effects.at(instructions.at(id)).must_defs.contains(other))
        continue;
      // With a closed entry contract, an unreachable execution context is
      // not an additional invocation. Unknown or unresolved transfers have
      // already rejected the matched-context proof before reaching here.
      if (!global->at(id).has_value())
        continue;
      const Facts after = transfer(global->at(id).value_or(Facts{}), id, false, true);
      for (const Id to : successors.at(id))
        starts.emplace_back(to, after);
    }
    const RegisterValueSet query{payload};
    return propagate(starts, &slice.relevant, &query, slice.retain).has_value();
  };
  for (const Pair& pair : pairs) {
    if (exhausted)
      break;
    if (dead_after_other_writes(pair.first, pair.second) &&
        dead_after_other_writes(pair.second, pair.first))
      proved.disjoint_pairs.insert(pair);
  }
  for (const GuardedDefinitionUseQuery& query : queries) {
    if (exhausted)
      break;
    if (query.definition >= ops.size() || query.use >= ops.size() ||
        query.register_name.empty() ||
        !effects.at(query.definition).must_defs.contains(query.register_name) ||
        !effects.at(query.use).uses.contains(query.register_name) ||
        ops.at(query.definition).meta.manual_interaction.has_value() ||
        ops.at(query.use).meta.manual_interaction.has_value())
      continue;
    const Slice& slice = get_slice(query.register_name, query.use);
    std::vector<Seed> starts;
    for (Id id = 0; id < instructions.size(); ++id) {
      if (instructions.at(id) != query.definition || !global->at(id).has_value())
        continue;
      const Facts after = transfer(*global->at(id), id, false, true);
      for (const Id to : successors.at(id))
        if (permits(id, to, *global->at(id)))
          starts.emplace_back(to, after);
    }
    const RegisterValueSet payload{query.register_name};
    if (!starts.empty() &&
        propagate(starts, &slice.relevant, &payload, slice.retain, query.use).has_value())
      proved.excluded_uses.insert(query);
  }
  if (std::getenv("MKPRO_NATIVE_TRACE_GUARDED_LIFETIMES") != nullptr) {
    std::cerr << "[guarded-register-lifetime] states=" << instructions.size()
              << " select-windows=" << windows.size() << " pairs=" << pairs.size()
              << " proved=" << proved.disjoint_pairs.size()
              << " excluded-uses=" << proved.excluded_uses.size()
              << " exhausted=" << exhausted << '\n';
    for (const auto& [left, right] : proved.disjoint_pairs)
      std::cerr << "[guarded-register-lifetime] disjoint " << left << " / " << right << '\n';
  }
  return proved;
}

// A bounded pushdown expansion keeps a return attached to its actual caller.
// Repeated loop iterations reuse the same state; the bound is on proof states,
// not iterations of the compiled program. Unsupported graphs use the original
// context-insensitive fixed point without changing any optimization premise.
std::optional<LivenessInfo> matched_call_liveness(const std::vector<IrOp>& ops,
                                                 LivenessOptions options) {
  if (ops.empty() ||
      (options.equal_entry_value_classes.size() < 2U &&
       std::none_of(ops.begin(), ops.end(), [](const IrOp& op) {
        return op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
      })) ||
      std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
        return op.meta.raw || op.kind == IrKind::OrphanAddress ||
               std::find(op.meta.roles.begin(), op.meta.roles.end(),
                         kResumableErrorPaddingRole) != op.meta.roles.end();
      })) {
    return std::nullopt;
  }

  const ControlFlowGraph graph =
      build_control_flow_graph(ops, BuildCfgOptions{.terminal_stop_fallthrough = false});
  if (!graph.targets_are_exact())
    return std::nullopt;

  const CfgTargetIndexes indexes = build_target_indexes(ops);
  const auto address_one = indexes.address_index.find(1);
  std::vector<std::size_t> call_returns;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (ops.at(index).kind == IrKind::Call || ops.at(index).kind == IrKind::IndirectCall) {
      if (index + 1U == ops.size())
        return std::nullopt;
      call_returns.push_back(index + 1U);
    }
  }

  struct ExecutionState {
    std::size_t instruction = 0;
    std::vector<std::size_t> returns;
    // Unreachable fragments are still analyzed. Their unmodeled caller prefix
    // returns conservatively to every call continuation, but calls made inside
    // the fragment retain their own matched suffix.
    bool unknown_prefix = false;
  };
  using Key = std::tuple<std::size_t, std::vector<std::size_t>, bool>;
  constexpr std::size_t kMaximumStates = 8192U;
  constexpr std::size_t kMaximumEdges = 65536U;
  constexpr std::size_t kMaximumReturnDepth = 5U;
  std::map<Key, std::size_t> identities;
  std::vector<ExecutionState> states;
  std::vector<std::vector<std::size_t>> successors;
  std::vector<bool> covered(ops.size(), false);
  bool failed = false;
  std::size_t edge_count = 0;
  const auto intern = [&](ExecutionState state) -> std::optional<std::size_t> {
    if (state.instruction >= ops.size() || state.returns.size() > kMaximumReturnDepth) {
      failed = true;
      return std::nullopt;
    }
    const Key key{state.instruction, state.returns, state.unknown_prefix};
    if (const auto existing = identities.find(key); existing != identities.end())
      return existing->second;
    if (states.size() >= kMaximumStates) {
      failed = true;
      return std::nullopt;
    }
    const std::size_t id = states.size();
    identities.emplace(key, id);
    covered.at(state.instruction) = true;
    states.push_back(std::move(state));
    successors.emplace_back();
    return id;
  };
  const auto connect = [&](std::size_t from, ExecutionState target) {
    const std::optional<std::size_t> to = intern(std::move(target));
    if (!to.has_value())
      return;
    auto& row = successors.at(from);
    if (std::find(row.begin(), row.end(), *to) == row.end()) {
      row.push_back(*to);
      if (++edge_count > kMaximumEdges)
        failed = true;
    }
  };

  (void)intern(ExecutionState{});
  std::vector<std::size_t> root_states{0U};
  std::size_t processed = 0;
  // Do not let physically present but unreachable code silently disappear
  // from the allocation proof. Seed such fragments only after all ordinary
  // entry contexts have been explored, avoiding fictitious roots in callees.
  for (std::size_t seed = 0; seed < ops.size() && !failed; ++seed) {
    if (!covered.at(seed)) {
      if (const auto root = intern(ExecutionState{.instruction = seed, .unknown_prefix = true}))
        root_states.push_back(*root);
    }
    while (processed < states.size() && !failed) {
      const std::size_t from = processed++;
      const ExecutionState state = states.at(from);
      const IrOp& op = ops.at(state.instruction);
      if (op.kind == IrKind::Return) {
        ExecutionState target = state;
        if (!target.returns.empty()) {
          target.instruction = target.returns.back();
          target.returns.pop_back();
          connect(from, std::move(target));
        } else if (state.unknown_prefix) {
          for (const std::size_t continuation : call_returns) {
            target.instruction = continuation;
            connect(from, target);
          }
          if (address_one != indexes.address_index.end()) {
            target.instruction = static_cast<std::size_t>(address_one->second);
            connect(from, target);
          }
        } else if (address_one != indexes.address_index.end()) {
          // An empty hardware return goes to physical 01, not main/00.
          target.instruction = static_cast<std::size_t>(address_one->second);
          connect(from, std::move(target));
        } else {
          failed = true;
        }
        continue;
      }
      const bool call = op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
      if (call && (state.returns.size() == kMaximumReturnDepth ||
                   graph.edges.at(state.instruction).empty())) {
        failed = true;
        continue;
      }
      for (const CfgEdge& edge : graph.edges.at(state.instruction)) {
        if (edge.target < 0 || static_cast<std::size_t>(edge.target) >= ops.size()) {
          failed = true;
          break;
        }
        ExecutionState target = state;
        target.instruction = static_cast<std::size_t>(edge.target);
        if (call)
          target.returns.push_back(state.instruction + 1U);
        connect(from, std::move(target));
      }
    }
  }
  if (failed)
    return std::nullopt;

  std::vector<RegisterEffects> effects;
  effects.reserve(ops.size());
  for (const IrOp& op : ops)
    effects.push_back(register_effects(op));
  const RegisterValueSet universe =
      register_universe(ops, effects, options.include_physical_register_universe);
  for (RegisterEffects& effect : effects) {
    if (effect.uses_all_registers)
      effect.uses.insert(universe.begin(), universe.end());
    if (effect.may_define_any_register)
      effect.may_defs.insert(universe.begin(), universe.end());
  }

  std::vector<std::vector<std::size_t>> predecessors(states.size());
  for (std::size_t from = 0; from < states.size(); ++from)
    for (const std::size_t to : successors.at(from))
      predecessors.at(to).push_back(from);
  std::vector<RegisterValueSet> live_in(states.size());
  std::vector<RegisterValueSet> live_out(states.size());
  std::deque<std::size_t> worklist;
  std::vector<bool> queued(states.size(), true);
  for (std::size_t id = states.size(); id-- > 0U;)
    worklist.push_back(id);
  while (!worklist.empty()) {
    const std::size_t id = worklist.front();
    worklist.pop_front();
    queued.at(id) = false;
    RegisterValueSet next_out;
    for (const std::size_t to : successors.at(id))
      next_out.insert(live_in.at(to).begin(), live_in.at(to).end());
    const RegisterEffects& effect = effects.at(states.at(id).instruction);
    RegisterValueSet next_in = effect.uses;
    for (const std::string& reg : next_out)
      if (!effect.must_defs.contains(reg))
        next_in.insert(reg);
    if (next_in == live_in.at(id) && next_out == live_out.at(id))
      continue;
    live_in.at(id) = std::move(next_in);
    live_out.at(id) = std::move(next_out);
    for (const std::size_t predecessor : predecessors.at(id)) {
      if (!queued.at(predecessor)) {
        queued.at(predecessor) = true;
        worklist.push_back(predecessor);
      }
    }
  }

  LivenessInfo result;
  result.live_in.resize(ops.size());
  result.live_out.resize(ops.size());
  result.includes_physical_register_universe = options.include_physical_register_universe;
  result.matched_call_contexts = true;
  std::vector<std::size_t> instructions;
  instructions.reserve(states.size());
  for (const ExecutionState& state : states)
    instructions.push_back(state.instruction);
  result.guarded_disjoint_pairs = guarded_register_lifetimes(
      ops, effects, instructions, successors, predecessors, root_states, options).disjoint_pairs;
  result.call_context_lifetimes.reserve(states.size());
  for (std::size_t id = 0; id < states.size(); ++id) {
    const std::size_t instruction = states.at(id).instruction;
    result.live_in.at(instruction).insert(live_in.at(id).begin(), live_in.at(id).end());
    result.live_out.at(instruction).insert(live_out.at(id).begin(), live_out.at(id).end());
    result.call_context_lifetimes.push_back(CallContextLifetime{
        .instruction = instruction,
        .live_in = std::move(live_in.at(id)),
        .live_out = std::move(live_out.at(id)),
    });
  }
  return result;
}

} // namespace

GuardedDefinitionUseProof analyze_unreachable_definition_uses(
    const std::vector<IrOp>& ops, const core::AuthoritativePostLayoutControlFlow& flow,
    const std::vector<GuardedDefinitionUseQuery>& queries, LivenessOptions options,
    std::size_t maximum_work) {
  if (queries.empty() || queries.size() > 64U || !flow.proved ||
      flow.execution_states.empty() ||
      flow.execution_states.size() != flow.execution_successors.size())
    return {};
  std::vector<RegisterEffects> effects;
  std::map<int, std::size_t> instruction_at;
  int address = 0;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].meta.raw || ops[i].meta.logical_register_analysis)
      return {};
    if (ops[i].kind != IrKind::Label)
      instruction_at.emplace(address, i);
    address += cells_per_op(ops[i]);
    effects.push_back(register_effects(ops[i]));
  }
  std::vector<std::size_t> instructions;
  std::vector<std::vector<std::size_t>> predecessors(flow.execution_states.size());
  std::vector<std::size_t> roots{0};
  for (std::size_t id = 0; id < flow.execution_states.size(); ++id) {
    const auto& state = flow.execution_states[id];
    const auto found = instruction_at.find(state.address);
    if (found == instruction_at.end())
      return {};
    instructions.push_back(found->second);
    for (const auto next : flow.execution_successors[id]) {
      if (next >= predecessors.size())
        return {};
      predecessors[next].push_back(id);
    }
    // Include every externally admissible return context. Resumed/manual
    // entries have only invariant setup facts, never a presumed old X value.
    for (const auto& entry : flow.external_entries) {
      if (entry.entry.address != state.address ||
          entry.return_stack.size() != state.return_stack.size())
        continue;
      bool same = true;
      for (std::size_t frame = 0; frame < state.return_stack.size(); ++frame)
        same = same && entry.return_stack[frame].address == state.return_stack[frame];
      if (same && id != 0U) {
        roots.push_back(id);
        break;
      }
    }
  }
  options.closed_program_entry = false;
  std::map<std::size_t,int> sign_facts;
  std::size_t sign_work=0;
  std::vector<std::size_t> sign_queries;
  for(std::size_t s=0;s<instructions.size();++s)
    if(ops[instructions[s]].kind==IrKind::Plain && ops[instructions[s]].opcode==0x32 &&
        std::find(ops[instructions[s]].meta.roles.begin(),ops[instructions[s]].meta.roles.end(),core::kUnitProjectionDemandRole)!=ops[instructions[s]].meta.roles.end())
      sign_queries.push_back(s);
  if(!sign_queries.empty()) {
    const auto items=lower_ir_to_machine(ops);
    const auto lift=analyze_post_layout_automatic_entry_lift(items,flow,maximum_work);
    std::vector<PreloadReport> preloads;
    for(const auto& [reg,word]:options.equal_entry_value_classes)
      if(word.starts_with("literal:")) preloads.push_back({.register_name=reg,.value=word.substr(8)});
    if(lift) {
      auto signs=core::prove_post_layout_packed_sign_inputs(items,flow,*lift,sign_queries,&preloads,maximum_work);
      sign_work=signs.work; if(signs.exhausted) return {{},sign_work,true};
      sign_facts=std::move(signs.signs);
    }
  }
  auto proof = guarded_register_lifetimes(ops, effects, instructions, flow.execution_successors,
      predecessors, roots, options, queries, maximum_work-std::min(maximum_work,sign_work), &sign_facts);
  return {proof.exhausted ? std::set<GuardedDefinitionUseQuery>{}
                          : std::move(proof.excluded_uses),
          proof.work+sign_work, proof.exhausted};
}

std::set<GuardedDefinitionUseQuery> prove_unreachable_definition_uses(
    const std::vector<IrOp>& ops, const core::AuthoritativePostLayoutControlFlow& flow,
    const std::vector<GuardedDefinitionUseQuery>& queries, LivenessOptions options) {
  return analyze_unreachable_definition_uses(ops, flow, queries, std::move(options)).excluded_uses;
}

PostLayoutRegisterDeadnessProof prove_post_layout_register_deadness(
    const std::vector<IrOp>& ops, const core::AuthoritativePostLayoutControlFlow& flow,
    std::size_t maximum_work) {
  PostLayoutRegisterDeadnessProof result;
  const auto fail = [&](const char* reason) {
    result.before_state.clear();
    result.reasons.emplace_back(reason);
    return result;
  };
  const auto items = lower_ir_to_machine(ops);
  const auto inputs = prove_post_layout_input_boundaries(items, flow);
  if (!inputs.proved)
    return fail("register lifetime proof requires validated input continuations");

  std::set<int> typed_protocols;
  for (const auto& item : items)
    if (!item.raw && item.manual_interaction &&
        item.manual_interaction->kind == ManualInteractionAnchorKind::PromptStop &&
        std::find(item.roles.begin(), item.roles.end(), kTypedDisplayObservationRole) !=
            item.roles.end())
      typed_protocols.insert(item.manual_interaction->protocol_id);

  std::map<std::size_t, RegisterEffects> effects;
  std::size_t machine = 0;
  for (auto op : ops) {
    if (op.kind == IrKind::Label) {
      ++machine;
      continue;
    }
    // A discarded source value may still reach the physical stack.
    op.meta.discarded_indirect_recall_value = false;
    auto effect = register_effects(op);
    const bool opaque = op.meta.raw || op.meta.logical_register_analysis ||
        op.kind == IrKind::OrphanAddress || op.opcode == 0x4f || op.opcode == 0x6f ||
        (op.opcode >= 0x70 && (op.opcode & 15) == 15) ||
        // Decimal entry only touches stack/entry state, never R0..Re.
        (op.kind == IrKind::Plain && op.opcode > 0x0f &&
         (opcode_by_code(op.opcode).risk == OpcodeRisk::Dangerous ||
          opcode_by_code(op.opcode).stack_effect == StackEffect::Unknown ||
          opcode_by_code(op.opcode).stack_effect == StackEffect::Barrier));
    if (opaque)
      effect.uses_all_registers = true;
    effects.emplace(machine, std::move(effect));
    machine += static_cast<std::size_t>(cells_per_op(op));
  }
  if (machine != items.size())
    return fail("register lifetime proof has an inconsistent command mapping");

  const auto universe = physical_register_universe();
  const auto count = flow.execution_states.size();
  std::vector<RegisterEffects> transfers;
  std::vector<std::vector<std::size_t>> predecessors(count);
  transfers.reserve(count);
  for (std::size_t state = 0; state < count; ++state) {
    const auto item_index = flow.execution_states[state].item_index;
    const auto found = effects.find(item_index);
    if (found == effects.end())
      return fail("register lifetime context does not reference a typed instruction");
    auto effect = found->second;
    const auto& item = items[item_index];
    if (item.manual_interaction) {
      const auto& anchor = *item.manual_interaction;
      if (!typed_protocols.contains(anchor.protocol_id) ||
          (anchor.kind != ManualInteractionAnchorKind::PromptStop &&
           !inputs.input_boundary_states[state]))
        effect.uses_all_registers = true;
    } else if (item.opcode == 0x50 || item.opcode == 0x29) {
      // Do not leave private memory unequal at arbitrary stops or exits.
      effect.uses_all_registers = true;
    }
    transfers.push_back(std::move(effect));
    for (const auto next : flow.execution_successors[state])
      predecessors[next].push_back(state);
  }

  // Greatest safety fixed point: a word can remain dead around a cycle.
  // Reads and untyped exits remove facts backwards; definite writes cut
  // the demand. A cycle with ANY path to an old-word observation is rejected.
  result.before_state.assign(count, universe);
  std::deque<std::size_t> work;
  std::vector<bool> queued(count, true);
  for (std::size_t state = count; state-- > 0;)
    work.push_back(state);
  while (!work.empty()) {
    if (++result.work > maximum_work) {
      result.exhausted = true;
      return fail("register lifetime proof budget exhausted");
    }
    const auto state = work.front();
    work.pop_front();
    queued[state] = false;
    const auto& effect = transfers[state];
    RegisterValueSet next;
    if (!effect.uses_all_registers && !effect.may_define_any_register) {
      const auto& successors = flow.execution_successors[state];
      if (!successors.empty()) {
        next = universe;
        for (const auto successor : successors) {
          if (++result.work > maximum_work) {
            result.exhausted = true;
            return fail("register lifetime proof budget exhausted");
          }
          RegisterValueSet common;
          const auto& suffix = result.before_state[successor];
          std::set_intersection(next.begin(), next.end(), suffix.begin(), suffix.end(),
                                std::inserter(common, common.end()));
          next = std::move(common);
        }
      }
      next.insert(effect.must_defs.begin(), effect.must_defs.end());
      for (const auto& used : effect.uses)
        next.erase(used);
    }
    if (next == result.before_state[state])
      continue;
    result.before_state[state] = std::move(next);
    for (const auto predecessor : predecessors[state])
      if (!queued[predecessor]) {
        queued[predecessor] = true;
        work.push_back(predecessor);
      }
  }
  result.proved = true;
  return result;
}

RegisterEffects register_effects(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return RegisterEffects{.must_defs = RegisterValueSet{op.register_name}};
  case IrKind::Recall:
    return RegisterEffects{.uses = RegisterValueSet{op.register_name}};
  case IrKind::IndirectRecall: {
    if (op.meta.discarded_indirect_recall_value) {
      RegisterEffects effects{.uses = RegisterValueSet{op.register_name}};
      add_indirect_selector_definition(effects, op);
      return effects;
    }
    const std::optional<std::set<std::string>> targets = known_indirect_memory_targets(op);
    RegisterEffects effects{
        .uses = RegisterValueSet{op.register_name},
        .uses_all_registers = !targets.has_value(),
    };
    if (targets.has_value())
      effects.uses.insert(targets->begin(), targets->end());
    add_indirect_selector_definition(effects, op);
    return effects;
  }
  case IrKind::IndirectStore: {
    const std::optional<std::set<std::string>> targets = known_indirect_memory_targets(op);
    RegisterEffects effects{.uses = RegisterValueSet{op.register_name}};
    if (!targets.has_value()) {
      effects.may_define_any_register = true;
    } else if (targets->size() == 1U) {
      effects.must_defs = *targets;
    } else {
      effects.may_defs = *targets;
    }
    add_indirect_selector_definition(effects, op);
    return effects;
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump: {
    RegisterEffects effects{.uses = RegisterValueSet{op.register_name}};
    add_indirect_selector_definition(effects, op);
    return effects;
  }
  case IrKind::Loop: {
    const std::string counter =
        op.meta.logical_register_analysis && op.meta.logical_register_name.has_value()
            ? *op.meta.logical_register_name
            : loop_counter_register(op.counter);
    if (counter.empty())
      return RegisterEffects{};
    return RegisterEffects{
        .uses = RegisterValueSet{counter},
        .must_defs = RegisterValueSet{counter},
    };
  }
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::Plain:
  case IrKind::OrphanAddress:
    return RegisterEffects{};
  }
  return RegisterEffects{};
}

LivenessInfo compute_liveness(const std::vector<IrOp>& ops, LivenessOptions options) {
  if (std::optional<LivenessInfo> matched = matched_call_liveness(ops, options))
    return std::move(*matched);

  if (!options.equal_entry_value_classes.empty() &&
      std::getenv("MKPRO_NATIVE_TRACE_GUARDED_LIFETIMES") != nullptr)
    std::cerr << "[guarded-register-lifetime] context fallback, ops=" << ops.size()
              << " calls=" << std::count_if(ops.begin(), ops.end(), [](const IrOp& op) {
                   return op.kind == IrKind::Call || op.kind == IrKind::IndirectCall;
                 }) << '\n';

  const ControlFlowGraph graph = build_control_flow_graph(
      ops, BuildCfgOptions{
               .indirect_call_fallthrough = true,
               .unknown_indirect_flow_to_all = options.unknown_indirect_flow_to_all,
               .unresolved_direct_flow_to_all = options.unresolved_direct_flow_to_all,
               .terminal_stop_fallthrough = false,
           });
  const std::size_t size = ops.size();
  std::vector<std::vector<int>> successors(size);
  std::vector<std::vector<int>> predecessors(size);
  for (std::size_t source = 0; source < graph.edges.size(); ++source) {
    for (const CfgEdge& edge : graph.edges.at(source)) {
      if (edge.target < 0 || edge.target >= static_cast<int>(size))
        continue;
      successors.at(source).push_back(edge.target);
      predecessors.at(static_cast<std::size_t>(edge.target)).push_back(static_cast<int>(source));
    }
  }

  std::vector<RegisterEffects> effects;
  effects.reserve(size);
  for (const IrOp& op : ops)
    effects.push_back(register_effects(op));
  const RegisterValueSet universe =
      register_universe(ops, effects, options.include_physical_register_universe);

  std::set<int> conservative_sources;
  for (const CfgUncertainty& uncertainty : graph.uncertainties)
    conservative_sources.insert(uncertainty.source);
  for (std::size_t index = 0; index < effects.size(); ++index) {
    RegisterEffects& op_effects = effects.at(index);
    if (op_effects.uses_all_registers || conservative_sources.contains(static_cast<int>(index))) {
      op_effects.uses.insert(universe.begin(), universe.end());
    }
    if (op_effects.may_define_any_register)
      op_effects.may_defs.insert(universe.begin(), universe.end());
  }

  std::vector<RegisterValueSet> live_in(size);
  std::vector<RegisterValueSet> live_out(size);

  std::deque<int> worklist;
  std::vector<bool> queued(size, true);
  for (int index = static_cast<int>(size) - 1; index >= 0; --index)
    worklist.push_back(index);

  while (!worklist.empty()) {
    const int index = worklist.front();
    worklist.pop_front();
    const std::size_t offset = static_cast<std::size_t>(index);
    queued.at(offset) = false;

    RegisterValueSet new_out;
    for (const int successor : successors.at(offset)) {
      const RegisterValueSet& successor_in = live_in.at(static_cast<std::size_t>(successor));
      new_out.insert(successor_in.begin(), successor_in.end());
    }

    const RegisterEffects& op_effects = effects.at(offset);
    RegisterValueSet new_in = op_effects.uses;
    for (const std::string& reg : new_out) {
      if (!op_effects.must_defs.contains(reg))
        new_in.insert(reg);
    }

    if (!sets_equal(new_in, live_in.at(offset)) || !sets_equal(new_out, live_out.at(offset))) {
      live_in.at(offset) = std::move(new_in);
      live_out.at(offset) = std::move(new_out);
      for (const int predecessor : predecessors.at(offset)) {
        const std::size_t predecessor_offset = static_cast<std::size_t>(predecessor);
        if (queued.at(predecessor_offset))
          continue;
        queued.at(predecessor_offset) = true;
        worklist.push_back(predecessor);
      }
    }
  }

  return LivenessInfo{
      .live_in = std::move(live_in),
      .live_out = std::move(live_out),
      .control_flow_targets_are_exact = graph.targets_are_exact(),
      .conservative_flow_sources =
          std::vector<int>(conservative_sources.begin(), conservative_sources.end()),
      .includes_physical_register_universe = options.include_physical_register_universe,
  };
}

bool RegisterInterferenceGraph::interferes(const std::string& left,
                                           const std::string& right) const {
  if (left == right)
    return false;
  const auto found = neighbors.find(left);
  return found != neighbors.end() && found->second.contains(right);
}

RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops,
                                                            const LivenessInfo& liveness) {
  if (liveness.live_in.size() != ops.size() || liveness.live_out.size() != ops.size())
    return build_register_interference_graph(ops);

  RegisterInterferenceGraph graph;
  std::vector<RegisterEffects> effects;
  effects.reserve(ops.size());

  bool needs_physical_universe = !liveness.conservative_flow_sources.empty();
  for (const IrOp& op : ops) {
    RegisterEffects op_effects = register_effects(op);
    needs_physical_universe = needs_physical_universe || op_effects.uses_all_registers ||
                              op_effects.may_define_any_register;
    for (const std::string& reg : op_effects.uses)
      graph.neighbors.try_emplace(reg);
    for (const std::string& reg : op_effects.must_defs)
      graph.neighbors.try_emplace(reg);
    for (const std::string& reg : op_effects.may_defs)
      graph.neighbors.try_emplace(reg);
    effects.push_back(std::move(op_effects));
  }
  for (const RegisterValueSet& live : liveness.live_in) {
    for (const std::string& reg : live)
      graph.neighbors.try_emplace(reg);
  }
  for (const RegisterValueSet& live : liveness.live_out) {
    for (const std::string& reg : live)
      graph.neighbors.try_emplace(reg);
  }
  if (needs_physical_universe && liveness.includes_physical_register_universe) {
    for (const std::string& reg : physical_register_universe())
      graph.neighbors.try_emplace(reg);
  }

  RegisterValueSet graph_universe;
  for (const auto& [reg, unused_neighbors] : graph.neighbors) {
    (void)unused_neighbors;
    graph_universe.insert(reg);
  }

  const auto contribute = [&](std::size_t index, const RegisterValueSet& live_in,
                              const RegisterValueSet& live_out) {
    add_clique(graph, live_in);
    add_clique(graph, live_out);
    RegisterValueSet definitions = effects.at(index).must_defs;
    definitions.insert(effects.at(index).may_defs.begin(), effects.at(index).may_defs.end());
    if (effects.at(index).may_define_any_register)
      definitions.insert(graph_universe.begin(), graph_universe.end());

    RegisterValueSet live_at_definition = live_in;
    live_at_definition.insert(live_out.begin(), live_out.end());
    add_clique(graph, definitions);
    for (const std::string& definition : definitions) {
      graph.neighbors.try_emplace(definition);
      for (const std::string& live : live_at_definition)
        add_interference(graph, definition, live);
    }
  };

  std::set<std::size_t> covered_context_instructions;
  for (const CallContextLifetime& row : liveness.call_context_lifetimes)
    covered_context_instructions.insert(row.instruction);
  const bool complete_contexts =
      liveness.matched_call_contexts && covered_context_instructions.size() == ops.size() &&
      (ops.empty() || *covered_context_instructions.rbegin() < ops.size());
  if (complete_contexts) {
    for (const CallContextLifetime& row : liveness.call_context_lifetimes)
      contribute(row.instruction, row.live_in, row.live_out);
  } else {
    for (std::size_t index = 0; index < ops.size(); ++index)
      contribute(index, liveness.live_in.at(index), liveness.live_out.at(index));
  }

  for (const auto& [left, right] : liveness.guarded_disjoint_pairs) {
    graph.neighbors[left].erase(right);
    graph.neighbors[right].erase(left);
  }
  return graph;
}

RegisterInterferenceGraph build_register_interference_graph(const std::vector<IrOp>& ops) {
  return build_register_interference_graph(ops, compute_liveness(ops));
}

} // namespace mkpro::core::passes
