#include "mkpro/core/passes/selector_charge_literal_sinking.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"
#include "mkpro/emulator/mk61.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace mkpro;
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
std::vector<MachineItem> fixture(int literal, char reg, bool live_other = false) {
  std::vector<MachineItem> items;
  const int register_index = reg - 'a' + 10;
  const auto op = [&](int code) {
    items.push_back(MachineItem::op(code, code <= 9 ? std::to_string(code) : ""));
  };
  const auto label = [&](const std::string& name) { items.push_back(MachineItem::label(name)); };
  const auto call = [&](const std::string& name) { op(0x53); items.push_back(MachineItem::address(name)); };
  const auto clear = [&] { op(0x0d); op(0x0e); op(0x0e); op(0x0e); };
  const auto charge = [&](const std::string& leaf, bool source) {
    op(0);
    items.back().roles = {"late-decimal-selector-high:" + leaf,
                         "late-decimal-selector-register:" + std::string(1, reg)};
    if (source) items.back().roles.push_back("selector-sunk-literal-source:shared");
    op(0);
    items.back().roles = {"late-decimal-selector-low:" + leaf,
                         "late-decimal-selector-register:" + std::string(1, reg)};
  };
  op(2); op(0x41); op(3); op(0x42);
  call("producer"); op(0x43); clear();
  call("alternate"); op(0x44); clear(); op(0x63);
  op(0x50); items.back().stop_disposition = StopDisposition::Terminal;
  label("alternate"); op(0x61); charge("discard", false); call("shared"); op(0x52);
  label("sum"); op(0x10); op(0x10); op(0x52);
  label("discard");
  if (live_other) { op(0x10); op(0x10); }
  else { clear(); op(0x10); }
  op(0x52);
  label("producer"); charge("sum", true);
  label("shared"); op(0x40 + register_index);
  items.back().roles = {"late-decimal-selector-store", "selector-sunk-literal-id:shared",
                        "selector-sunk-literal-value:" + std::to_string(literal)};
  for (char digit : std::to_string(literal)) op(digit - '0');
  op(0x61); op(0x62); op(0xa0 + register_index);
  items.back().roles = {"late-decimal-selector-consumer"};
  items.back().indirect_flow_targets = std::vector<IrTarget>{"sum", "discard"};
  op(0x52);
  const auto bound = core::rebind_late_bound_decimal_selectors(items, {.minimum_target_address = 0});
  if (!bound.diagnostics.empty())
    throw std::runtime_error("fixture selector must bind: " + bound.diagnostics.front().message);
  return bound.items;
}
auto optimize(const std::vector<MachineItem>& items) {
  return core::passes::optimize_selector_data_literal_coalescing(items, {},
      core::build_post_layout_control_flow(items), core::NaturalTargetComponentLayoutOptions{});
}
std::vector<std::string> run(const std::vector<MachineItem>& items, const std::string& seed,
                             const std::vector<PreloadReport>& preloads = {}) {
  const auto resolved = resolve_machine_items(items);
  require(resolved.diagnostics.empty(), "fixture must resolve");
  std::vector<int> bytes;
  for (const auto& step : resolved.steps) bytes.push_back(step.opcode);
  emulator::MK61 calc;
  require(calc.load_program(bytes).diagnostics.empty(), "fixture must fit hardware");
  for (const auto& name : {"X", "Y", "Z", "T", "X1"}) calc.set_register(name, seed);
  for (const auto& preload : preloads) calc.set_register(preload.register_name, preload.value);
  calc.press_sequence({"В/О", "С/П"});
  require(calc.run_until_stable(5000, 6).stopped, "fixture must return and stop");
  std::vector<std::string> state;
  // R7 is a private selector, whose internal leading-zero representation
  // changes on indirect access. R8 observes its data value through a real
  // program recall; that observation, not private formatting, must agree.
  for (const auto& name : {"X", "Y", "Z", "T", "X1", "1", "2", "3", "4", "8"})
    state.push_back(calc.read_register(name));
  calc.press("."); state.push_back(calc.display_text(true));
  return state;
}
} // namespace
int main() {
  try {
    for (char reg : {'a', 'd'}) {
      for (int literal : {20, 26, 32}) {
        const auto original = fixture(literal, reg);
        const auto result = optimize(original);
        if (result.applied != 1 || result.removed_cells != 2 || !result.final_control_flow.proved) {
          std::string message = "joint layout failed for " + std::to_string(literal) +
              "/" + reg + ": applied=" + std::to_string(result.applied) +
              ", removed=" + std::to_string(result.removed_cells);
          for (const auto& reason : result.reasons) message += "; " + reason;
          throw std::runtime_error(message);
        }
        require(optimize(result.items).applied == 0, "coalescing must reach a fixed point");
        for (const std::string seed : {"0", "-7", "3.25"})
          require(run(original, seed) == run(result.items, seed),
                  "joint layout must preserve ROM results, stack, X1 and X2");
      }
    }
    require(optimize(fixture(20, 'a', true)).applied == 0,
            "an alternate mode which needs the displaced input must reject");
    require(optimize(fixture(80, 'a')).applied == 0,
            "placement padding must not outweigh the removed literal");
    require(optimize(fixture(2, 'a')).applied == 0,
            "an impossible absolute target must not change the program");
    auto preimage = fixture(98, 'a');
    for (int i = 0; i < 58; ++i) preimage.push_back(MachineItem::op(0x54, "NOP"));
    preimage.push_back(MachineItem::op(0x52, "return"));
    const auto preimage_result = optimize(preimage);
    require(preimage_result.applied > 0 && preimage_result.removed_cells == 2,
            "a provisional address 100 must become the delivered data/selector address 98 after erasure");
    for (const std::string seed : {"0", "-7", "3.25"})
      require(run(preimage, seed) == run(preimage_result.items, seed),
              "deferred selector binding must preserve ROM observations");
    // An unrelated stable indirect call must follow its command identity,
    // not the old physical address, through the same literal erasure.
    for (const int literal : {20, 27, 33}) {
      auto companion = fixture(literal, 'a');
      companion.insert(companion.end() - 1, MachineItem::label("companion_return"));
      auto call = MachineItem::op(0xa7, "KPP7");
      call.indirect_flow_targets = std::vector<IrTarget>{"companion_return"};
      companion.insert(companion.begin(), call);
      const auto binding = core::rebind_late_bound_decimal_selectors(
          companion, {.minimum_target_address = 0});
      require(binding.diagnostics.empty(), "companion fixture must bind");
      companion = binding.items;
      int address = 0;
      int target = -1;
      for (const auto& item : companion) {
        if (item.kind != MachineItemKind::Label) ++address;
        else if (item.name == "companion_return") target = address;
      }
      require(target >= 0 && target <= 99, "companion fixture must be addressable");
      const std::vector<PreloadReport> preloads{{.register_name = "7",
                                               .value = std::to_string(target)}};
      const auto folded = core::passes::optimize_selector_data_literal_coalescing(
          companion, preloads, core::build_post_layout_control_flow(companion), {});
      if (literal == 20) {
        require(folded.applied == 0,
                "a 21-cell main cannot yield a profitable leaf at address 20");
        continue;
      }
      if (folded.applied <= 0 || folded.removed_cells <= 0) {
        std::string reason = "companion literal=" + std::to_string(literal);
        for (const auto& detail : folded.reasons) reason += "; " + detail;
        throw std::runtime_error(reason);
      }
      require(folded.preloads.front().value != preloads.front().value,
              "the fixture must exercise actual companion preload relocation");
      for (const std::string seed : {"0", "-7", "3.25"})
        require(run(companion, seed, preloads) == run(folded.items, seed, folded.preloads),
                "companion retargeting must preserve ROM stack, X1, X2 and return state");
      auto stale = preloads;
      stale.front().value = "104";
      require(core::passes::optimize_selector_data_literal_coalescing(
          companion, stale, core::build_post_layout_control_flow(companion), {}).applied == 0,
          "an undecodable companion preload must not become a relocation proof");
    }
    for (const bool tail : {false, true}) {
      auto indirect_entry = fixture(tail ? 24 : 25, 'a');
      bool replaced = false;
      for (std::size_t i = 1; i < indirect_entry.size(); ++i) {
        if (indirect_entry[i].kind != MachineItemKind::Address ||
            indirect_entry[i].target != IrTarget{std::string("shared")}) continue;
        indirect_entry[i - 1] = MachineItem::op(tail ? 0x87 : 0xa7, "indirect entry");
        indirect_entry[i - 1].indirect_flow_targets = std::vector<IrTarget>{"shared"};
        indirect_entry.erase(indirect_entry.begin() + static_cast<std::ptrdiff_t>(i));
        if (tail) {
          require(indirect_entry[i].opcode == 0x52, "tail fixture must end in return");
          indirect_entry.erase(indirect_entry.begin() + static_cast<std::ptrdiff_t>(i));
        }
        replaced = true;
        break;
      }
      require(replaced, "fixture must have a shared-entry call");
      const auto binding = core::rebind_late_bound_decimal_selectors(
          indirect_entry, {.minimum_target_address = 0});
      require(binding.diagnostics.empty(), "indirect entry fixture must bind");
      indirect_entry = binding.items;
      int address = 0;
      int target = -1;
      for (const auto& item : indirect_entry) {
        if (item.kind != MachineItemKind::Label) ++address;
        else if (item.name == "shared") target = address;
      }
      const std::vector<PreloadReport> preloads{{.register_name = "7",
                                               .value = std::to_string(target)}};
      const auto folded = core::passes::optimize_selector_data_literal_coalescing(
          indirect_entry, preloads, core::build_post_layout_control_flow(indirect_entry), {});
      require(folded.applied > 0 && folded.removed_cells > 0,
              "an indirect shared-entry transfer must retain numeric charge provenance");
      for (const std::string seed : {"0", "-7", "3.25"})
        require(run(indirect_entry, seed, preloads) == run(folded.items, seed, folded.preloads),
                "indirect entry coalescing must preserve ROM stack and return contexts");
    }
    auto raw = fixture(26, 'a'); raw.front().raw = true;
    require(optimize(raw).applied == 0, "raw code must fail closed");
    auto missing = fixture(26, 'a');
    for (auto& item : missing)
      std::erase(item.roles, "selector-sunk-literal-source:shared");
    require(optimize(missing).applied == 0, "missing data provenance must reject");
    auto observed = fixture(26, 'a');
    for (std::size_t i = 0; i < observed.size(); ++i)
      if (observed[i].kind == MachineItemKind::Label && observed[i].name == "sum") {
        observed[i + 1] = MachineItem::op(0x6a, "recall selector"); break;
      }
    require(optimize(observed).applied == 0, "ordinary selector data use must reject");
    auto hidden = fixture(26, 'a');
    for (std::size_t i = 0; i < hidden.size(); ++i)
      if (hidden[i].kind == MachineItemKind::Label && hidden[i].name == "discard") {
        hidden[i + 1] = MachineItem::op(0x0f, "last X"); break;
      }
    require(optimize(hidden).applied == 0, "observable physical last-X must reject");

    // The alternate procedure is anchored inside the required data interval.
    // Its selector is also externally observed data, so retuning it is wrong.
    for (const bool tail_jump : {false, true}) {
    auto conflict = fixture(20, 'a');
    bool replaced = false;
    for (std::size_t i = 1; i < conflict.size(); ++i) {
      if (conflict[i].kind != MachineItemKind::Address ||
          conflict[i].target != IrTarget{std::string("alternate")}) continue;
      if (tail_jump) {
        conflict[i].target = std::string("bridge");
        conflict.push_back(MachineItem::label("bridge"));
        conflict.push_back(MachineItem::op(0x87, "KBP7"));
        conflict.back().indirect_flow_targets = std::vector<IrTarget>{"alternate"};
      } else {
        conflict[i - 1] = MachineItem::op(0xa7, "KPP7");
        conflict[i - 1].indirect_flow_targets = std::vector<IrTarget>{"alternate"};
        conflict.erase(conflict.begin() + static_cast<std::ptrdiff_t>(i));
      }
      replaced = true; break;
    }
    require(replaced, "fixture must own the alternate call");
    for (std::size_t i = 0; i < conflict.size(); ++i) {
      if (conflict[i].kind != MachineItemKind::Label || conflict[i].name != "alternate") continue;
      conflict.insert(conflict.begin() + static_cast<std::ptrdiff_t>(i + 1),
          {MachineItem::op(0x67, "R7"), MachineItem::op(0x48, "store R8")});
      break;
    }
    auto rebound_conflict = core::rebind_late_bound_decimal_selectors(conflict,
        {.minimum_target_address = 0});
    require(rebound_conflict.diagnostics.empty(), "conflicting fixture must bind");
    conflict = rebound_conflict.items;
    const std::vector<PreloadReport> preloads{{.register_name = "7", .value = tail_jump ? "20" : "19"}};
    const auto frontier = core::passes::selector_data_literal_layout_frontier(
        conflict, preloads, core::build_post_layout_control_flow(conflict), {});
    require(!frontier.alternatives.empty(), frontier.reasons.empty() ?
        "conflicting selector must be released" : frontier.reasons.front().c_str());
    const auto best = std::min_element(frontier.alternatives.begin(), frontier.alternatives.end(),
        [](const auto& left, const auto& right) { return left.removed_cells > right.removed_cells; });
    require(best->removed_cells >= 1, "joint transaction must repay call expansion and save cells");
    require(std::any_of(best->optimizations.begin(), best->optimizations.end(), [](const auto& value) {
      return value.name == "selector-anchor-release-transaction";
    }), "the conflicting immutable call family must be explicitly released");
    for (const std::string seed : {"0", "-7", "3.25"}) {
      const auto expected = run(conflict, seed, preloads);
      const auto actual = run(best->items, seed, best->preloads);
      if (expected != actual) {
        for (std::size_t i = 0; i < expected.size(); ++i)
          if (expected[i] != actual[i])
            std::cerr << "anchor release seed=" << seed << " observation=" << i
                      << " expected=[" << expected[i] << "] actual=[" << actual[i] << "]\n";
        for (const auto& value : best->preloads)
          std::cerr << "preload " << value.register_name << '=' << value.value << '\n';
        throw std::runtime_error("anchor release must preserve observed selector data, stack and returns in ROM");
      }
    }
    auto mismatched_preload = preloads;
    mismatched_preload.front().value = "22";
    require(core::passes::selector_data_literal_layout_frontier(conflict, mismatched_preload,
        core::build_post_layout_control_flow(conflict), {}).alternatives.empty(),
        "a preload which does not deliver the typed target must reject");
    auto written = conflict;
    written[1] = MachineItem::op(0x47, "store R7");
    require(core::passes::selector_data_literal_layout_frontier(written, preloads,
        core::build_post_layout_control_flow(written), {}).alternatives.empty(),
        "a runtime-written selector is not an immutable call family");
    }
    // Erasing the literal shifts an immutable, data-observed selector target.
    // The final order can restore that address without padding, but laying out
    // the larger pre-erasure components cannot meet the same constraints.
    for (const int prefix : {0, 1}) {
      auto original = fixture(23 + prefix, 'a');
      const auto sum = std::find_if(original.begin(), original.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Label && item.name == "sum";
      });
      const auto discard = std::find_if(sum, original.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Label && item.name == "discard";
      });
      std::vector<MachineItem> sum_body(sum, discard);
      original.erase(sum, discard);
      const auto producer = std::find_if(original.begin(), original.end(), [](const auto& item) {
        return item.kind == MachineItemKind::Label && item.name == "producer";
      });
      original.insert(producer, {MachineItem::label("spare_return"),
                                 MachineItem::op(0x52, "return")});
      original.push_back(MachineItem::label("fixed_return"));
      original.push_back(MachineItem::op(0x52, "return"));
      original.insert(original.end(), sum_body.begin(), sum_body.end());
      auto fixed_call = MachineItem::op(0xa7, "KPP7");
      fixed_call.indirect_flow_targets = std::vector<IrTarget>{"fixed_return"};
      original.insert(original.begin(), {MachineItem::op(0x67, "R7"),
          MachineItem::op(0x48, "R8"), fixed_call});
      for (int i = 0; i < prefix; ++i)
        original.insert(original.begin(), MachineItem::op(0x54, "NOP"));
      const auto bound = core::rebind_late_bound_decimal_selectors(
          original, {.minimum_target_address = 0});
      require(bound.diagnostics.empty(), "erasure-aware fixture must bind");
      original = bound.items;
      const std::vector<PreloadReport> preloads{
          {.register_name = "7", .value = std::to_string(45 + prefix)}};
      const auto result = core::passes::optimize_selector_data_literal_coalescing(
          original, preloads, core::build_post_layout_control_flow(original), {});
      if (result.applied <= 0 || result.removed_cells < 2) {
        std::string message = "erasure-aware layout must save both literal cells";
        for (const auto& reason : result.reasons) message += "; " + reason;
        throw std::runtime_error(message);
      }
      require(std::any_of(result.optimizations.begin(), result.optimizations.end(),
          [](const auto& report) {
            return report.name == "selector-data-literal-erasure-layout";
          }), "regression must exercise deletion-aware placement");
      require(result.preloads.size() == 1 && result.preloads.front().value == preloads.front().value,
              "observable companion data must remain unchanged");
      for (const std::string seed : {"0", "-7", "3.25"})
        require(run(original, seed, preloads) == run(result.items, seed, result.preloads),
                "erasure-aware layout must preserve ROM stack, X1/X2, data and return contexts");
      auto stale = preloads;
      stale.front().value = "104";
      require(core::passes::optimize_selector_data_literal_coalescing(
          original, stale, core::build_post_layout_control_flow(original), {}).applied == 0,
          "deferred reconciliation must reject a stale immutable preload");
      auto duplicate = preloads;
      duplicate.push_back(preloads.front());
      require(core::passes::optimize_selector_data_literal_coalescing(
          original, duplicate, core::build_post_layout_control_flow(original), {}).applied == 0,
          "ambiguous immutable preload provenance must reject");
    }
    std::cout << "selector data/literal coalescing compiler and ROM tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
