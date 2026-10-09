#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/joint_representation_search.hpp"
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::size_t query = 0, old_definition = 0, new_definition = 0, read = 0;
  bool resume = false;
};
struct Variant {
  bool indirect = false, inverted = false, exit_guard = true, bypass = false;
  bool selecting = true, two_contexts = false, resume = false, alias = false;
};

Fixture fixture(int payload, int guard, Variant variant = {}) {
  MachineEmitter e;
  std::map<std::string, std::string> selectors;
  const auto branch = [&](int code, const std::string& target, const std::string& selector) {
    if (!variant.indirect) {
      e.emit_jump(code, "condition", target);
      return;
    }
    const int reg = selector == "e" ? 14 : 12;
    const int indirect = code == 0x5e ? 0xe0 : code == 0x57 ? 0x70 :
                         code == 0x5c ? 0xc0 : 0x90;
    e.emit_op(indirect + reg);
    e.items.back().indirect_flow_targets = std::vector<IrTarget>{target};
    selectors[selector] = target;
  };
  e.emit_jump(0x53, "call", "work");
  e.emit_stop(StopDisposition::Terminal);
  e.emit_label("work");
  e.emit_op(0x0d);
  e.emit_number("-1");
  e.emit_op(0x40 + guard);
  e.emit_op(0x67);
  e.emit_op(0x40 + payload, std::nullopt, "old");
  if (variant.two_contexts)
    e.emit_jump(0x53, "old context", "check");
  if (variant.resume)
    e.emit_stop(StopDisposition::Resumable);
  e.emit_op(0x69);
  e.emit_op(0x60 + guard);
  e.emit_op(variant.selecting ? 0x36 : 0x10);
  e.emit_op(0x40 + guard);
  e.emit_op(0x11);
  branch(variant.inverted ? 0x57 : 0x5e, variant.inverted ? "assign" : "next", "e");
  if (variant.inverted)
    e.emit_jump(0x51, "skip", "next");
  e.emit_label("assign");
  if (variant.bypass) {
    e.emit_op(0x66);
    e.emit_jump(0x5e, "bypass", "next");
  }
  e.emit_op(0x0d);
  e.emit_number("4");
  e.emit_op(0x40 + payload, std::nullopt, "new");
  e.emit_label("next");
  e.emit_jump(0x53, "selected context", "check");
  e.emit_op(0x52);
  e.emit_label("check");
  if (variant.exit_guard) {
    e.emit_op(0x60 + guard);
    branch(variant.inverted ? 0x59 : 0x5c, variant.inverted ? "negative" : "live", "c");
    if (variant.inverted)
      e.emit_jump(0x51, "positive", "live");
    e.emit_label("negative");
    e.emit_number("0");
    e.emit_op(0x52);
  }
  e.emit_label("live");
  if (variant.alias) {
    e.emit_op(0x67);
    e.emit_op(0xbd);
    e.items.back().indirect_memory_targets = std::vector<int>{payload};
  }
  e.emit_op(0x60 + payload, std::nullopt, "read");
  e.emit_op(0x31, std::nullopt, "query");
  e.emit_op(0x52);
  Fixture f{.items = e.items, .resume = variant.resume};
  std::map<std::string, int> labels;
  int address = 0;
  for (std::size_t i = 0; i < f.items.size(); ++i) {
    const auto& item = f.items[i];
    if (item.kind == MachineItemKind::Label) labels[item.name] = address;
    else ++address;
    if (item.comment == "query") f.query = i;
  }
  for (const auto& [reg, target] : selectors)
    f.preloads.push_back({.register_name = reg, .value = std::to_string(labels.at(target))});
  if (variant.alias)
    f.preloads.push_back({.register_name = "d", .value = std::to_string(payload)});
  const auto ops = raise_machine_to_ir(f.items);
  for (std::size_t i = 0; i < ops.size(); ++i) {
    if (ops[i].meta.comment == "old") f.old_definition = i;
    if (ops[i].meta.comment == "new") f.new_definition = i;
    if (ops[i].meta.comment == "read") f.read = i;
  }
  return f;
}

PostLayoutNumericRangeProof ranges(const Fixture& f, std::size_t budget = 100000,
    NumericRangeInputPolicy policy = NumericRangeInputPolicy::ValidatedRegisterTransport) {
  const auto flow = build_post_layout_control_flow(f.items);
  if (!flow.proved)
    return prove_post_layout_numeric_ranges(f.items, flow, {}, {f.query},
                                           &f.preloads, budget, policy);
  const auto lift = analyze_post_layout_automatic_entry_lift(f.items, flow, 100000);
  require(lift.has_value(), "guarded range fixture entry lift");
  return prove_post_layout_numeric_ranges(f.items, flow, *lift, {f.query},
                                         &f.preloads, budget, policy);
}

std::vector<int> bytecode(const std::vector<MachineItem>& items) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "guarded fixture must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  return bytes;
}
double displayed_number(std::string value) {
  std::replace(value.begin(), value.end(), ',', '.');
  return std::stod(value);
}
std::vector<std::string> observe(const Fixture& f, int candidate, const std::string& seed) {
  emulator::MK61 calc;
  require(calc.load_program(bytecode(f.items)).diagnostics.empty(), "guarded fixture fits ROM");
  for (int reg = 0; reg < 15; ++reg)
    calc.set_register(std::string(1, "0123456789abcde"[reg]), std::to_string(reg + 2));
  for (const auto& preload : f.preloads)
    calc.set_register(preload.register_name, preload.value);
  for (const char* reg : {"X", "Y", "Z", "T", "X1", "7"}) calc.set_register(reg, seed);
  calc.set_register("9", std::to_string(candidate));
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(5000, 8).stopped, "guarded fixture stops");
  if (f.resume) {
    calc.press_sequence({"5", "\u0421/\u041f"});
    require(calc.run_until_stable(5000, 8).stopped, "guarded fixture resumes");
  }
  std::vector<std::string> result;
  for (const char* reg : {"X", "Y", "Z", "T", "X1"}) result.push_back(calc.read_register(reg));
  calc.press(".");
  result.push_back(calc.display_text(true));
  for (int reg = 0; reg < 15; ++reg)
    result.push_back(calc.read_register(std::string(1, "0123456789abcde"[reg])));
  return result;
}

void selection_proofs() {
  for (int payload : {1, 2, 3})
    for (bool indirect : {false, true})
      for (bool inverted : {false, true}) {
        const auto f = fixture(payload, 8, {.indirect = indirect, .inverted = inverted});
        const auto proof = ranges(f);
        require(proof.x_before.contains(f.query) &&
                    proof.x_before.at(f.query) == OrdinaryNumericRange{4, 4, true},
                "guarded selected payload must exclude the previous unknown epoch");
        require(ranges(f).x_before == proof.x_before && ranges(f).work == proof.work,
                "guarded refinement must be deterministic");
        const auto ops = raise_machine_to_ir(f.items);
        const passes::GuardedDefinitionUseQuery old{f.old_definition, f.read,
                                                    ops[f.read].register_name};
        const passes::GuardedDefinitionUseQuery selected{f.new_definition, f.read,
                                                         ops[f.read].register_name};
        const auto flow = build_post_layout_control_flow(f.items);
        const auto exclusions = passes::analyze_unreachable_definition_uses(
            ops, flow, {old, selected});
        require(exclusions.excluded_uses.contains(old) &&
                    !exclusions.excluded_uses.contains(selected),
                "direct/indirect and inverted tests must retain the live definition");
        const auto exhausted = passes::analyze_unreachable_definition_uses(
            ops, flow, {old, selected}, {}, 1);
        require(exhausted.exhausted && exhausted.work <= 1 && exhausted.excluded_uses.empty(),
                "predicate budget cannot export a partial certificate");
        for (int candidate : {-2, -1, 0, 1, 9}) {
          const auto observed = observe(f, candidate, "-13.5");
          require(displayed_number(observed.front()) == (candidate < 0 ? 0. : 4.),
                  "ROM must confirm that the stale payload never reaches the guarded read");
        }
      }
  const auto contexts = fixture(2, 10, {.indirect = true, .two_contexts = true});
  require(ranges(contexts).x_before.at(contexts.query) == OrdinaryNumericRange{4, 4, true},
          "a wholly infeasible matched call context must not poison another call");
  for (int candidate : {0, 2, 7})
    require(displayed_number(observe(contexts, candidate, "-17").front()) == 4.,
            "matched-context exclusion must agree with actual returns");
  const auto resumed = fixture(1, 8, {.indirect = true, .resume = true});
  require(ranges(resumed).x_before.at(resumed.query) == OrdinaryNumericRange{4, 4, true} &&
              displayed_number(observe(resumed, 3, "-17").front()) == 4.,
          "typed resume must preserve registers, not seed new setup facts");
  auto bounded_old = fixture(1, 8);
  bounded_old.preloads.push_back({.register_name = "7", .value = "17"});
  require(ranges(bounded_old).x_before.at(bounded_old.query) == OrdinaryNumericRange{4, 4, true},
          "guarded proof must refine a known but overly wide interval too");
}

void rejected_proofs() {
  for (const auto& f : {
           fixture(1, 8, {.exit_guard = false}),
           fixture(2, 10, {.indirect = true, .bypass = true}),
           fixture(3, 8, {.selecting = false}),
           fixture(1, 8, {.indirect = true, .alias = true})})
    require(!ranges(f).x_before.contains(f.query),
            "missing guard, optional assignment, nonselect and alias writes must remain unknown");
  auto raw = fixture(1, 8);
  raw.items.front().raw = true;
  require(!ranges(raw).x_before.contains(raw.query), "raw entry cannot authorize guarded facts");
  auto opaque = fixture(1, 8);
  opaque.items[opaque.query - 1].opcode = 0x4f;
  require(!ranges(opaque).x_before.contains(opaque.query), "unknown opcode cannot borrow a register fact");
  const auto f = fixture(2, 8);
  const auto limited = ranges(f, 1);
  require(limited.exhausted && limited.x_before.empty(), "range budget must fail closed");
  require(!ranges(f, 100000, NumericRangeInputPolicy::Forget).x_before.contains(f.query),
          "IR fragments cannot silently adopt complete-program input transport");
  auto flow = build_post_layout_control_flow(f.items);
  const auto lift = analyze_post_layout_automatic_entry_lift(f.items, flow, 100000);
  flow.proved = false;
  require(prove_post_layout_numeric_ranges(f.items, flow, *lift, {f.query}, &f.preloads)
              .x_before.empty(), "unknown CFG cannot carry guarded ranges");
  auto entry = f;
  entry.items.insert(entry.items.begin(), MachineItem::address("live"));
  entry.items.insert(entry.items.begin(), MachineItem::op(0x51, "external entry"));
  entry.query += 2;
  require(!ranges(entry).x_before.contains(entry.query),
          "an externally admitted payload with no reaching store cannot be discarded");
}

void append(std::vector<MachineItem>& items, std::initializer_list<int> codes) {
  for (int code : codes) items.push_back(MachineItem::op(code, "guarded composition fixture"));
}
void stop(std::vector<MachineItem>& items) {
  append(items, {0x50}); items.back().stop_disposition = StopDisposition::Terminal;
}
int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const auto& item) {
    return item.kind != MachineItemKind::Label;
  }));
}
Fixture composed(int carrier) {
  Fixture f;
  append(f.items, {0x45, 1, 0x40 + carrier});
  for (int repeat = 0; repeat < 3; ++repeat)
    append(f.items, {0x6b, 0x60 + carrier, 1, 0, 0x13, 0x12, 0x4a});
  append(f.items, {0x0d, 1, 0x0b, 0x48, 0x6d, 0x47, 0x69, 0x68, 0x36, 0x48, 0x11, 0x5e});
  f.items.push_back(MachineItem::address("next"));
  append(f.items, {0x0d, 4, 0x47});
  f.items.push_back(MachineItem::label("next"));
  append(f.items, {0x68, 0x5c}); f.items.push_back(MachineItem::address("live"));
  append(f.items, {0}); stop(f.items);
  f.items.push_back(MachineItem::label("live")); append(f.items, {0x67});
  f.query = f.items.size();
  append(f.items, {0x60 + carrier, 0x36, 0x40 + carrier, 0x60 + carrier, 0x44,
                   0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10});
  stop(f.items);
  return f;
}
void representation_composition() {
  for (int carrier : {2, 3}) {
    const auto before = composed(carrier);
    CompileOptions options; options.defer_return_suffix_until_callee_hole = true;
    const auto candidates = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before.items), {options},
        {.maximum_candidates = 32, .runtime_preloads = &before.preloads});
    const auto selected = std::find_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
      return std::any_of(candidate.optimizations.begin(), candidate.optimizations.end(),
          [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
    });
    require(selected != candidates.end(), "representation synthesis must consume guarded ranges");
    auto after = before;
    after.items = lower_ir_to_machine(selected->ops);
    require(cells(before.items) == 58 && cells(after.items) == 51,
            "guarded range/representation size baseline must remain 58 to 51");
    const auto complete = expand_joint_representation_transactions(
        {.items = before.items, .preloads = before.preloads}, {}, true);
    require(std::any_of(complete.begin(), complete.end(), [](const auto& edge) {
      return cells(edge.node.items) == 51;
    }), "the complete-artifact frontier must compose the proof with word synthesis");
    for (int candidate : {0, 1, 9})
      for (const std::string seed : {"0", "-7", "3.25"})
        require(observe(before, candidate, seed) == observe(after, candidate, seed),
                "composed rewrite must preserve registers, stack, X1, X2 and returns");
  }
}

void sign_stack_rom_facts() {
  const std::vector<std::vector<int>> entries{
      {}, {2, 3}, {2, 0x0a, 3}, {2, 0x0c, 3}, {2, 0x0c, 0x0b, 3},
      {0x10}, {0x0e}, {0x0d}, {0x14}, {0x0b}, {0x25}};
  const auto lower_stack = [](const auto& entry, const char* word, bool sign) {
    emulator::MK61 calc({.extended = false});
    std::vector<int> code{0x67, 0x68, 0x69, 0x6a, 0x6b};
    code.insert(code.end(), entry.begin(), entry.end());
    if (sign) code.push_back(0x0b);
    code.push_back(0x50);
    require(calc.load_program(code).diagnostics.empty(), "sign-mode ROM fixture fits stock memory");
    calc.set_register("7", "11"); calc.set_register("8", "17");
    calc.set_register("9", "23"); calc.set_register("a", "29");
    calc.set_register("b", word);
    calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
    require(calc.run_until_stable(2000, 8).stopped, "sign-mode ROM fixture stops");
    std::vector<std::string> result;
    for (const char* reg : {"Y", "Z", "T"}) result.push_back(calc.read_register(reg));
    return result;
  };
  for (const auto& entry : entries)
    for (const char* word : {"-7.25", "0", "8.8888888", "99999999", "1E-99"})
      require(lower_stack(entry, word, true) == lower_stack(entry, word, false),
              "mantissa, exponent and recalled sign edits preserve Y/Z/T");
}

void sign_projection_range_contracts() {
  for (int slot = 1; slot <= 3; ++slot) {
    Fixture f;
    append(f.items, {0x68});
    for (int push = 0; push < slot; ++push) append(f.items, {0x69});
    append(f.items, {0x0b});
    for (int turn = 0; turn < slot; ++turn) append(f.items, {0x25});
    f.query = f.items.size();
    stop(f.items);
    f.preloads = {{.register_name = "8", .value = "17"}};
    require(ranges(f).x_before.at(f.query) == OrdinaryNumericRange{17, 17, true},
            "an unknown edited X cannot erase an independent lower-stack range");
    auto unknown = f;
    unknown.preloads.clear();
    require(!ranges(unknown).x_before.contains(unknown.query),
            "stack transport cannot create a missing entry value");
  }

  Fixture recalled;
  append(recalled.items, {0x69, 0x0b});
  recalled.query = recalled.items.size();
  stop(recalled.items);
  recalled.preloads = {{.register_name = "9", .value = "-3"}};
  require(ranges(recalled).x_before.at(recalled.query) == OrdinaryNumericRange{3, 3, true},
          "the recalled-number context certifies numeric negation");

  for (const auto& entry : {
           std::vector<int>{2, 0x0c, 3}, std::vector<int>{0x0c}}) {
    Fixture exponent;
    append(exponent.items, {0x69});
    for (int code : entry) append(exponent.items, {code});
    append(exponent.items, {0x0b});
    exponent.query = exponent.items.size();
    stop(exponent.items);
    exponent.preloads = recalled.preloads;
    require(!ranges(exponent).x_before.contains(exponent.query),
            "exponent-sign editing is not a proved arithmetic negation of X");
  }
}

Fixture sign_composition(int carrier, int divisor, bool called, bool unknown = false,
                         bool negative = false) {
  Fixture f;
  append(f.items, {0x45, 1, 0x40 + carrier});
  for (int repeat = 0; repeat < 3; ++repeat) {
    append(f.items, {0x6b, 0x60 + carrier, 1, 0});
    if (divisor == 100) append(f.items, {0});
    append(f.items, {0x13, 0x12, 0x4a});
  }
  if (called) {
    append(f.items, {0x53});
    f.items.push_back(MachineItem::address(std::string{"reduction_input"}));
  } else {
    append(f.items, {0x6d, 0x6e, 0x0b, 0x10});
  }
  f.query = f.items.size();
  append(f.items, {0x60 + carrier, 0x36, 0x40 + carrier, 0x60 + carrier, 0x44,
                   0x6b, 0x6c, 0x6d, 0x6e, 0x6b, 0x10});
  stop(f.items);
  if (called) {
    f.items.push_back(MachineItem::label("reduction_input"));
    append(f.items, {0x6d, 0x6e, 0x0b, 0x10, 0x52});
  }
  if (!unknown)
    f.preloads = {{.register_name = "d", .value = negative ? "1" : "17"},
                  {.register_name = "e", .value = negative ? "3" : "-3"}};
  return f;
}

void sign_representation_composition() {
  CompileOptions options;
  options.defer_return_suffix_until_callee_hole = true;
  const auto mixed = [](const auto& proposal) {
    return std::any_of(proposal.optimizations.begin(), proposal.optimizations.end(),
        [](const auto& report) { return report.name == "mixed-epoch-coefficient-state-carrier"; });
  };
  for (int carrier : {2, 3})
    for (int divisor : {10, 100})
      for (bool called : {false, true}) {
        const auto before = sign_composition(carrier, divisor, called);
        const auto fact = ranges(before);
        require(fact.x_before.at(before.query) == OrdinaryNumericRange{20, 20, true},
                "preserved accumulator and signed recall prove the reduction input");
        const auto proposals = passes::finite_mode_representation_candidates(
            raise_machine_to_ir(before.items), {options},
            {.maximum_candidates = 32, .runtime_preloads = &before.preloads});
        const auto selected = std::find_if(proposals.begin(), proposals.end(), mixed);
        require(selected != proposals.end(),
                "stack projections must enable the independent representation synthesizer");
        auto after = before;
        after.items = lower_ir_to_machine(selected->ops);
        const int original_size = (divisor == 10 ? 40 : 43) + (called ? 3 : 0);
        const int final_size = (divisor == 10 ? 33 : 34) + (called ? 3 : 0);
        require(cells(before.items) == original_size && cells(after.items) == final_size,
                "sign/range/encoding composition must save seven or nine cells");
        for (const std::string seed : {"0", "-7", "3.25"})
          require(observe(before, 9, seed) == observe(after, 9, seed),
                  "composed rewrite preserves registers, stack, X1, X2 and return behavior");
        const auto complete = expand_joint_representation_transactions(
            {.items = before.items, .preloads = before.preloads}, {}, true);
        require(std::any_of(complete.begin(), complete.end(), [&](const auto& edge) {
                  return cells(edge.node.items) == final_size;
                }), "the complete-artifact frontier consumes the same range certificate");
      }
  for (bool unknown : {false, true}) {
    const auto before = sign_composition(2, 10, true, unknown, !unknown);
    const auto proposals = passes::finite_mode_representation_candidates(
        raise_machine_to_ir(before.items), {options},
        {.maximum_candidates = 32, .runtime_preloads = &before.preloads});
    require(std::none_of(proposals.begin(), proposals.end(), mixed),
            "unknown or negative reduction operands cannot absorb a recoded carrier");
  }
}
} // namespace

int main() {
  try {
    selection_proofs();
    rejected_proofs();
    representation_composition();
    sign_stack_rom_facts();
    sign_projection_range_contracts();
    sign_representation_composition();
    std::cout << "guarded numeric range compiler/ROM and composed 58-to-51, 40-to-33, 43-to-34 contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
