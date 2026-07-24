#include "mkpro/core/runtime_cost.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

std::vector<MachineItem> terminal_program(int opcode, const char* mnemonic) {
  std::vector<MachineItem> items;
  items.push_back(MachineItem::op(opcode, mnemonic));
  MachineItem stop = MachineItem::op(0x50, "С/П");
  stop.stop_disposition = StopDisposition::Terminal;
  items.push_back(std::move(stop));
  return items;
}

} // namespace

void runtime_cost_model_prefers_faster_equal_size_candidates() {
  const auto& table = core::mk61_opcode_runtime_cost_centiseconds();
  require(std::all_of(table.begin(), table.end(),
                      [](int cost) { return cost > 0; }),
          "every MK-61 opcode should have one positive runtime-cost entry");
  require(table[0x10] < table[0x1c],
          "ordinary addition should be cheaper than a trigonometric command");

  const RuntimeCostReport fast =
      core::estimate_runtime_cost(terminal_program(0x10, "+"));
  const RuntimeCostReport slow =
      core::estimate_runtime_cost(terminal_program(0x1c, "F sin"));
  require(fast.available && slow.available,
          "straight-line terminal artifacts should have authoritative cost estimates");
  require(fast.score < slow.score,
          "equal-cell synthetic alternatives should retain their opcode timing difference");
  require(core::runtime_cost_candidate_is_better(2, fast, 2, slow),
          "the faster proof-valid equal-cell candidate should win");
  require(!core::runtime_cost_candidate_is_better(3, fast, 2, slow),
          "runtime cost must never override a one-cell size disadvantage");
  const std::optional<std::string> reason =
      core::runtime_cost_tie_break_reason(2, fast, 2, slow);
  require(reason.has_value() &&
              reason->find("equal 2-cell") != std::string::npos,
          "an equal-size speed decision should expose a deterministic tie-break reason");

  std::vector<MachineItem> loop;
  loop.push_back(MachineItem::op(0x51, "БП"));
  loop.push_back(MachineItem::address(0));
  const RuntimeCostReport cyclic = core::estimate_runtime_cost(loop);
  require(cyclic.available && cyclic.cyclic_sccs == 1 &&
              cyclic.loop_traversal_bound == core::kRuntimeCostLoopTraversalBound,
          "a proved loop should use the documented fixed SCC traversal policy");

  std::vector<MachineItem> unknown_indirect;
  unknown_indirect.push_back(MachineItem::op(0x80, "К БП 0"));
  const RuntimeCostReport unavailable =
      core::estimate_runtime_cost(unknown_indirect);
  require(!unavailable.available,
          "an indirect transfer without a complete target proof must not influence tie-breaks");
}

} // namespace mkpro::tests
