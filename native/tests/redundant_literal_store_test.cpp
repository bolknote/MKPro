#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/contextual_copy_transport.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

int cells(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
      [](const auto& item) { return item.kind != MachineItemKind::Label; }));
}

void arithmetic(MachineEmitter& e, int code, int n = 4) {
  const int a = code == 0x10 ? n - 1 : code == 0x11 ? n + 2 : code == 0x12 ? n : 2 * n;
  const int b = code == 0x10 || code == 0x12 ? 1 : 2;
  e.emit_number(std::to_string(a));
  e.emit_op(0x0e);
  e.emit_number(std::to_string(b));
  e.emit_op(code);
}

void kill_stack(MachineEmitter& e) {
  for (int i = 0; i < 4; ++i) e.emit_op(0x67);
}

std::vector<MachineItem> fixture(int code = 0x10, bool observed = false, int n = 4) {
  MachineEmitter e;
  arithmetic(e, code, n);
  e.emit_number("4");
  e.emit_op(0x48);
  if (observed) { e.emit_op(0x14); e.emit_op(0x49); }
  e.emit_op(0x0f); // Explicitly observe the unchanged physical last-X.
  e.emit_op(0x4a);
  kill_stack(e);
  e.emit_stop(StopDisposition::Terminal);
  return e.items;
}

std::vector<std::string> observe(const std::vector<MachineItem>& items) {
  const auto image = resolve_machine_items(items);
  require(image.diagnostics.empty(), "literal fixture resolves");
  std::vector<int> bytes;
  for (const auto& step : image.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "literal fixture fits stock MK-61");
  for (int r = 0; r < 15; ++r)
    calc.set_register(std::string(1, "0123456789abcde"[r]), std::to_string(20 + r));
  calc.set_register("X", "31"); calc.set_register("Y", "41");
  calc.set_register("Z", "53"); calc.set_register("T", "67");
  calc.press_sequence({"\u0412/\u041e", "\u0421/\u041f"});
  require(calc.run_until_stable(5000, 8).stopped, "literal fixture stops");
  std::vector<std::string> result;
  for (const char* name : {"X", "Y", "Z", "T"}) result.push_back(calc.read_register(name));
  for (const char r : std::string("0123456789abcde"))
    result.push_back(calc.read_register(std::string(1, r)));
  return result;
}

std::vector<passes::ContextualCopyTransportCandidate> candidates(
    const std::vector<MachineItem>& items, std::size_t work = 200000) {
  CompileOptions options;
  return passes::contextual_redundant_literal_store_candidates(
      raise_machine_to_ir(items), {options}, {.maximum_work = work, .maximum_candidates = 8});
}

void positive_and_negative() {
  for (const int op : {0x10, 0x11, 0x12, 0x13}) {
    const auto original = fixture(op);
    const auto proposed = candidates(original);
    require(!proposed.empty(), "canonical arithmetic result can replace the same literal");
    for (const auto& candidate : proposed) {
      const auto changed = lower_ir_to_machine(candidate.transformation.ops);
      require(cells(changed) == cells(original) - 1, "one real cell is removed");
      require(observe(original) == observe(changed), "ROM preserves stack and every physical memory word");
    }
  }
  require(candidates(fixture(0x10, false, 5)).empty(), "a different numeric value cannot replace a literal");
  require(candidates(fixture(0x10, true)).empty(), "an observed removed stack lift cannot be erased");
  require(candidates(fixture(), 0).empty(), "exhausted proofs export no candidate");
  auto raw = fixture();
  auto literal = std::find_if(raw.begin(), raw.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 4;
  });
  require(literal != raw.end(), "literal found");
  literal->raw = true;
  require(candidates(raw).empty(), "raw code is not a canonical-word proof");

  MachineEmitter open;
  open.emit_number("44"); open.emit_op(0x48); kill_stack(open);
  open.emit_stop(StopDisposition::Terminal);
  require(candidates(open.items).empty(), "a trailing digit in open numeric entry is not a fresh literal");
  MachineEmitter zero;
  arithmetic(zero, 0x11, 0); zero.emit_number("0"); zero.emit_op(0x48); kill_stack(zero);
  zero.emit_stop(StopDisposition::Terminal);
  require(candidates(zero.items).empty(), "zero is outside the certified canonical small-word domain");
}

void entry_shadow_is_not_value_equality() {
  for (const bool exponent : {false, true}) {
    MachineEmitter e;
    arithmetic(e, 0x10);
    const auto literal = e.items.size();
    e.emit_number("4");
    e.emit_op(exponent ? 0x0c : 0x0a);
    e.emit_op(exponent ? 2 : 6); // A real digit key, not a new complete literal.
    e.emit_op(0x4a);
    kill_stack(e); e.emit_stop(StopDisposition::Terminal);
    auto erased = e.items;
    erased.erase(erased.begin() + static_cast<std::ptrdiff_t>(literal));
    require(observe(e.items) != observe(erased),
        "ROM entry-shadow witness distinguishes equal X before dot/VP");
    require(candidates(e.items).empty(),
        "literal erasure without its unchanged store cannot assume X2 equality");
    auto stored = e.items;
    stored.insert(stored.begin() + static_cast<std::ptrdiff_t>(literal + 1U),
                  MachineItem::op(0x48, "X->P 8"));
    require(candidates(stored).empty(),
        "a retained store is not itself an X2 equality certificate");
  }
}

void unsupported_entry_stays_unknown() {
  for (const auto& spelling : {std::vector<int>{1, 0x0a, 2},
                               std::vector<int>{1, 0x0c, 2},
                               std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9}}) {
    MachineEmitter e;
    e.emit_op(0x0e);
    for (const auto code : spelling) e.emit_op(code);
    const auto query = e.items.size();
    e.emit_stop(StopDisposition::Terminal);
    const auto flow = build_post_layout_control_flow(e.items);
    auto lift = analyze_post_layout_automatic_entry_lift(e.items, flow, 200000);
    require(lift.has_value(), "unsupported-entry fixture has complete lift analysis");
    std::fill(lift->begin(), lift->end(), false);
    const auto proof = prove_post_layout_stack_numeric_values(e.items, flow, *lift, {query});
    require(!proof.exhausted && !proof.x_before.contains(query),
        "Enter does not legalize fractions, exponent edits or overlong literals");
  }
}

void all_callers() {
  for (const bool disagree : {false, true}) {
    MachineEmitter e;
    arithmetic(e, 0x10); e.emit_jump(0x53, "first caller", "shared");
    arithmetic(e, 0x10, disagree ? 5 : 4); e.emit_jump(0x53, "second caller", "shared");
    kill_stack(e); e.emit_stop(StopDisposition::Terminal);
    e.emit_label("shared"); e.emit_number("4"); e.emit_op(0x48); kill_stack(e); e.emit_op(0x52);
    const auto proposed = candidates(e.items);
    require(proposed.empty() == disagree, "every exact return context must prove the same incoming word");
    for (const auto& candidate : proposed)
      require(observe(e.items) == observe(lower_ir_to_machine(candidate.transformation.ops)),
          "ROM preserves both caller continuations and saved return addresses");
  }
}

void separate_control_certificate() {
  const auto original = fixture();
  const auto where = std::find_if(original.begin(), original.end(), [](const auto& item) {
    return item.kind == MachineItemKind::Op && item.opcode == 4;
  });
  require(where != original.end(), "control fixture has a literal");
  const auto index = static_cast<std::size_t>(where - original.begin());
  auto changed = original;
  changed.erase(changed.begin() + static_cast<std::ptrdiff_t>(index));
  std::vector<std::optional<std::size_t>> mapping(original.size());
  for (std::size_t i = 0; i < original.size(); ++i) mapping[i] = i - (i > index ? 1U : 0U);
  const auto before = build_post_layout_control_flow(original);
  const auto after = build_post_layout_control_flow(changed);
  PostLayoutExecutionRelocationOptions old;
  old.erased_stack_producer_item = index;
  require(!prove_post_layout_execution_relocation(original, changed, before, after, mapping, old).proved,
      "the old recall/Enter control certificate does not authorize literal erasure");
  PostLayoutExecutionRelocationOptions literal;
  literal.erased_redundant_literal_item = index;
  require(prove_post_layout_execution_relocation(original, changed, before, after, mapping, literal).proved,
      "a separate literal certificate preserves the retained store and rooted continuations");
  auto malformed = original;
  malformed[index].opcode = 0;
  require(!prove_post_layout_execution_relocation(malformed, changed,
      build_post_layout_control_flow(malformed), after, mapping, literal).proved,
      "the separate literal control certificate still excludes zero");
}

std::string discriminated_word(int n, int op, int discriminator) {
  MachineEmitter e;
  if (op < 0) e.emit_number(std::to_string(n)); else arithmetic(e, op, n);
  e.emit_op(0x0e);
  if (discriminator == 0) { e.emit_number("11111111"); e.emit_op(0x39); }
  if (discriminator == 1) { e.emit_op(0x0c); e.emit_op(7); }
  if (discriminator == 2) { e.emit_op(0x40); e.emit_op(0xd0); }
  e.emit_stop(StopDisposition::Terminal);
  return observe(e.items).front();
}

void canonical_arithmetic_rom_fact() {
  for (int n = 1; n <= 9; ++n)
    for (int discriminator = 0; discriminator < 3; ++discriminator)
      for (const int op : {0x10, 0x11, 0x12, 0x13})
        require(discriminated_word(n, -1, discriminator) == discriminated_word(n, op, discriminator),
            "ROM arithmetic result 1..9 has the fresh digit's complete word, not only its printed value");
}
} // namespace

int main() {
  try {
    positive_and_negative();
    entry_shadow_is_not_value_equality();
    unsupported_entry_stays_unknown();
    all_callers();
    separate_control_certificate();
    canonical_arithmetic_rom_fact();
    std::cout << "redundant literal store: compiler and ROM checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
