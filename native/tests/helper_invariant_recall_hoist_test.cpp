#include "mkpro/core/helper_invariant_recall_hoist.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

constexpr std::string_view kRoot = "q7";

void append_forget_y(std::vector<MachineItem>& items) {
  // K AND/K OR retain Y.  Three stack lifts push the one deliberately
  // reordered Y value out of X/Y/Z/T without evaluating it; the first lift
  // also overwrites X2 from the already-equal X value.
  items.push_back(MachineItem::op(0x0e, "В↑"));
  items.push_back(MachineItem::op(0x0e, "В↑"));
  items.push_back(MachineItem::op(0x0e, "В↑"));
}

std::vector<MachineItem> alpha_fixture() {
  std::vector<MachineItem> items;

  // Two before-call forms.
  items.push_back(MachineItem::op(0x68, "П→X 8"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(std::string(kRoot)));
  items.push_back(MachineItem::op(0x38, "К ∨"));
  append_forget_y(items);
  items.push_back(MachineItem::op(0x40, "X→П 0"));

  items.push_back(MachineItem::op(0x68, "П→X 8"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(std::string(kRoot)));
  items.push_back(MachineItem::op(0x37, "К ∧"));
  append_forget_y(items);
  items.push_back(MachineItem::op(0x42, "X→П 2"));

  // One after-return form.  The symbolic proof, rather than a comment or a
  // source identifier, proves that the commutative join and the three lifts
  // erase the changed stack order.  The unrelated R7 immediately before PP
  // must not hide the globally common R8 candidate on the other side.
  items.push_back(MachineItem::op(0x67, "П→X 7"));
  items.push_back(MachineItem::op(0x53, "ПП"));
  items.push_back(MachineItem::address(std::string(kRoot)));
  items.push_back(MachineItem::op(0x68, "П→X 8"));
  items.push_back(MachineItem::op(0x38, "К ∨"));
  append_forget_y(items);
  items.push_back(MachineItem::op(0x43, "X→П 3"));
  items.push_back(MachineItem::op(0x50, "С/П"));

  // The stop is also the official fallthrough fence for the root.  The body
  // pushes a value computed solely from a different direct register.  A
  // zero-width preceding procedure-end marker is metadata, not another entry.
  MachineItem end_metadata = MachineItem::label("m2");
  end_metadata.procedure_boundary = "end";
  items.push_back(end_metadata);
  items.push_back(MachineItem::label(std::string(kRoot)));
  items.push_back(MachineItem::op(0x61, "П→X 1"));
  items.push_back(MachineItem::op(0x22, "F x²"));
  items.push_back(MachineItem::op(0x52, "В/О"));
  return items;
}

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

int opcode_count(const std::vector<MachineItem>& items, int opcode) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [&](const MachineItem& item) {
    return item.kind == MachineItemKind::Op && item.opcode == opcode;
  }));
}

int item_address(const std::vector<MachineItem>& items, std::size_t wanted_item) {
  int address = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (index == wanted_item)
      return address;
    if (items.at(index).kind != MachineItemKind::Label)
      ++address;
  }
  throw std::runtime_error("fixture item is absent");
}

std::size_t label_index(const std::vector<MachineItem>& items, std::string_view name) {
  for (std::size_t index = 0; index < items.size(); ++index)
    if (items.at(index).kind == MachineItemKind::Label && items.at(index).name == name)
      return index;
  throw std::runtime_error("fixture label is absent");
}

bool contains_reason(const core::HelperInvariantRecallHoistProof& proof, std::string_view needle) {
  return std::any_of(proof.reasons.begin(), proof.reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

std::string compact(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](unsigned char ch) { return std::isspace(ch) != 0; }),
              value.end());
  return value;
}

struct EmulatorOutcome {
  bool stopped = false;
  std::array<std::string, 7> values;

  bool operator==(const EmulatorOutcome&) const = default;
};

EmulatorOutcome run(const std::vector<MachineItem>& items) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "recall-hoist fixture should resolve");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);

  emulator::MK61 calc({.extended = true, .angle_mode = "deg"});
  const emulator::ProgramLoadResult loaded = calc.load_program(codes);
  require(loaded.diagnostics.empty(), "recall-hoist fixture should load");
  calc.set_register("1", "3");
  calc.set_register("8", "4");
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult stable = calc.run_until_stable(1200, 6);
  return EmulatorOutcome{
      .stopped = stable.stopped,
      .values = {compact(calc.read_register("x")), compact(calc.read_register("y")),
                 compact(calc.read_register("z")), compact(calc.read_register("t")),
                 compact(calc.read_register("0")), compact(calc.read_register("2")),
                 compact(calc.read_register("3"))},
  };
}

std::vector<MachineItem> helper_with_inserted_op(int opcode) {
  std::vector<MachineItem> items = alpha_fixture();
  const std::size_t root = label_index(items, kRoot);
  items.insert(items.begin() + static_cast<std::ptrdiff_t>(root + 1U),
               MachineItem::op(opcode, "alpha"));
  return items;
}

std::vector<MachineItem> noncommutative_after_return_fixture() {
  std::vector<MachineItem> items = alpha_fixture();
  for (std::size_t index = 0; index + 3U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x53 &&
        items.at(index + 1U).kind == MachineItemKind::Address &&
        items.at(index + 2U).kind == MachineItemKind::Op && items.at(index + 2U).opcode == 0x68) {
      items.at(index + 3U) = MachineItem::op(0x11, "−");
      return items;
    }
  }
  throw std::runtime_error("after-return fixture site is absent");
}

std::vector<MachineItem> forward_jump_continuation_fixture() {
  std::vector<MachineItem> items = alpha_fixture();
  for (std::size_t index = 0; index + 4U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x53 &&
        items.at(index + 1U).kind == MachineItemKind::Address &&
        items.at(index + 2U).kind == MachineItemKind::Op &&
        items.at(index + 2U).opcode == 0x68 &&
        items.at(index + 3U).kind == MachineItemKind::Op &&
        items.at(index + 3U).opcode == 0x38) {
      items.insert(items.begin() + static_cast<std::ptrdiff_t>(index + 4U),
                   {MachineItem::op(0x51, "БП"),
                    MachineItem::address("after_forward_join"),
                    MachineItem::label("after_forward_join")});
      return items;
    }
  }
  throw std::runtime_error("forward-jump continuation fixture site is absent");
}

std::vector<MachineItem> x2_live_fixture() {
  std::vector<MachineItem> items = alpha_fixture();
  for (std::size_t index = 0; index + 6U < items.size(); ++index) {
    if (items.at(index).kind == MachineItemKind::Op && items.at(index).opcode == 0x53 &&
        items.at(index + 1U).kind == MachineItemKind::Address &&
        items.at(index + 2U).kind == MachineItemKind::Op && items.at(index + 2U).opcode == 0x68 &&
        items.at(index + 3U).kind == MachineItemKind::Op && items.at(index + 3U).opcode == 0x38) {
      items.at(index + 4U) = MachineItem::op(0x0f, "F Вx");
      items.at(index + 5U) = MachineItem::op(0x0d, "Cx");
      items.at(index + 6U) = MachineItem::op(0x0a, ".");
      return items;
    }
  }
  throw std::runtime_error("X2-live fixture site is absent");
}

} // namespace

void helper_invariant_recall_hoist_rewrites_only_proved_calls() {
  const std::vector<MachineItem> baseline = alpha_fixture();
  const core::HelperInvariantRecallHoistProof proof =
      core::verify_helper_invariant_recall_hoist(baseline, std::string(kRoot));
  require(proof.proved && proof.reasons.empty(),
          "three alpha call sites should pass the bounded recall-hoist proof");
  require(proof.calls.size() == 3U && proof.recall_opcode == 0x68 && proof.register_index == 8 &&
              proof.helper_body_cells == 2U,
          "proof should expose the complete call set, register, and helper body");
  require(
      std::count_if(proof.calls.begin(), proof.calls.end(),
                    [](const auto& call) {
                      return call.placement == core::HelperInvariantRecallPlacement::BeforeCall;
                    }) == 2 &&
          std::count_if(proof.calls.begin(), proof.calls.end(),
                        [](const auto& call) {
                          return call.placement ==
                                 core::HelperInvariantRecallPlacement::AfterReturnBeforeCommutative;
                        }) == 1,
      "proof should distinguish the two legal recall placements");

  const core::HelperInvariantRecallHoistResult rewritten =
      core::rewrite_helper_invariant_recall_hoist(baseline, std::string(kRoot));
  require(rewritten.applied == 1 && rewritten.proof.final_artifact_proved,
          "proved recall hoist should survive the final artifact check");
  require(cell_count(rewritten.items) == cell_count(baseline) - 2 &&
              opcode_count(baseline, 0x68) == 3 && opcode_count(rewritten.items, 0x68) == 1,
          "three call-site recalls should become one helper-root recall and save two cells");
  const std::size_t rewritten_root = label_index(rewritten.items, kRoot);
  require(rewritten_root + 1U < rewritten.items.size() &&
              rewritten.items.at(rewritten_root + 1U).kind == MachineItemKind::Op &&
              rewritten.items.at(rewritten_root + 1U).opcode == 0x68,
          "the retained recall should be the first helper command");
  require(run(baseline) == run(rewritten.items),
          "baseline and hoisted alpha fixtures should be emulator-equivalent");

  {
    const std::vector<MachineItem> forward = forward_jump_continuation_fixture();
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(forward, std::string(kRoot));
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved,
            "bounded symbolic continuation should follow a proved forward direct jump");
    require(run(forward) == run(accepted.items),
            "forward-jump recall hoist should remain emulator-equivalent");
  }

  {
    std::vector<MachineItem> second_entry = alpha_fixture();
    const std::size_t root = label_index(second_entry, kRoot);
    second_entry.insert(second_entry.begin() + static_cast<std::ptrdiff_t>(root + 2U),
                        MachineItem::label("z4"));
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(second_entry, std::string(kRoot));
    require(rejected.applied == 0 && rejected.items.size() == second_entry.size() &&
                contains_reason(rejected.proof, "second executable entry"),
            "a second helper entry should fail closed");
  }

  for (const int opcode : {0x68, 0x48}) {
    const std::vector<MachineItem> access = helper_with_inserted_op(opcode);
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(access, std::string(kRoot));
    require(rejected.applied == 0 &&
                contains_reason(rejected.proof, "reads or writes the register"),
            "helper read/write of the hoisted register should fail closed");
  }

  {
    const std::vector<MachineItem> noncommutative = noncommutative_after_return_fixture();
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(noncommutative, std::string(kRoot));
    require(rejected.applied == 0 &&
                contains_reason(rejected.proof, "no single direct-register recall"),
            "a noncommutative after-return consumer should fail closed");
  }

  {
    const std::vector<MachineItem> x2_live = x2_live_fixture();
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(x2_live, std::string(kRoot));
    require(rejected.applied == 0 &&
                contains_reason(rejected.proof, "X2 is observed before a proved overwrite"),
            "an X2 restore before the symbolic kill should fail closed");
  }

  {
    std::vector<MachineItem> proved_indirect = alpha_fixture();
    const std::size_t root = label_index(proved_indirect, kRoot);
    std::size_t main_stop = root;
    while (main_stop > 0) {
      --main_stop;
      if (proved_indirect.at(main_stop).kind == MachineItemKind::Op &&
          proved_indirect.at(main_stop).opcode == 0x50) {
        break;
      }
    }
    proved_indirect.insert(proved_indirect.begin() + static_cast<std::ptrdiff_t>(main_stop),
                           MachineItem::op(0x80, "К БП 0"));
    const std::size_t old_flow_item = main_stop;
    const int old_target = item_address(proved_indirect, main_stop + 1U);
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(old_flow_item, std::vector<int>{old_target});
    const auto accepted =
        core::rewrite_helper_invariant_recall_hoist(proved_indirect, std::string(kRoot), options);
    const std::size_t new_flow_item = old_flow_item - 3U;
    require(accepted.applied == 1 && accepted.proof.final_artifact_proved &&
                accepted.proof.final_indirect_flow_targets.contains(new_flow_item) &&
                accepted.proof.final_indirect_flow_targets.at(new_flow_item) ==
                    std::vector<int>{old_target - 3},
            "a complete unrelated indirect-flow proof should be retained and reindexed");
  }

  {
    std::vector<MachineItem> removed_target = alpha_fixture();
    removed_target.insert(removed_target.begin(), MachineItem::op(0x80, "К БП 0"));
    std::size_t first_recall = 0;
    while (first_recall < removed_target.size() &&
           (removed_target.at(first_recall).kind != MachineItemKind::Op ||
            removed_target.at(first_recall).opcode != 0x68)) {
      ++first_recall;
    }
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(
        0U, std::vector<int>{item_address(removed_target, first_recall)});
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(removed_target, std::string(kRoot), options);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "removed call-site recall"),
            "an indirect target into a removed recall should fail closed");
  }

  {
    std::vector<MachineItem> helper_target = alpha_fixture();
    helper_target.insert(helper_target.begin(), MachineItem::op(0x80, "К БП 0"));
    const std::size_t root = label_index(helper_target, kRoot);
    core::HelperInvariantRecallHoistOptions options;
    options.proved_indirect_flow_targets.emplace(
        0U, std::vector<int>{item_address(helper_target, root)});
    const auto rejected =
        core::rewrite_helper_invariant_recall_hoist(helper_target, std::string(kRoot), options);
    require(rejected.applied == 0 && contains_reason(rejected.proof, "can enter the helper body"),
            "an indirect target into the helper should fail closed");
  }

  {
    std::vector<MachineItem> indirect = alpha_fixture();
    indirect.insert(indirect.begin(), MachineItem::op(0x80, "К БП 0"));
    const auto rejected = core::rewrite_helper_invariant_recall_hoist(indirect, std::string(kRoot));
    require(rejected.applied == 0 && contains_reason(rejected.proof, "unknown indirect flow"),
            "unknown indirect flow should prevent a complete entry proof");
  }
}

} // namespace mkpro::tests
