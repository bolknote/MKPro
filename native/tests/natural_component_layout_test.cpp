#include "mkpro/core/natural_component_layout.hpp"
#include "mkpro/core/shared_helper_continuation.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::tests {

namespace {

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::size_t item_at_address(const std::vector<MachineItem>& items, int wanted) {
  int address = 0;
  for (std::size_t item = 0; item < items.size(); ++item) {
    if (items.at(item).kind == MachineItemKind::Label)
      continue;
    if (address == wanted)
      return item;
    ++address;
  }
  throw std::runtime_error("fixture address is absent: " + std::to_string(wanted));
}

int label_address(const std::vector<MachineItem>& items, const std::string& label) {
  int address = 0;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label && item.name == label)
      return address;
    if (item.kind != MachineItemKind::Label)
      ++address;
  }
  return -1;
}

std::vector<MachineItem> fixture(std::string helper = "value_kernel") {
  std::vector<MachineItem> items = {
      MachineItem::op(0x52, "В/О"), // 00: required only by physical layout C
      MachineItem::op(0x60, "П->X 0"),

      // Ordinary call A followed by Q = commutative join + direct store.
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(helper),
      MachineItem::op(0x38, "К ∨"),
      MachineItem::op(0x49, "X->П 9"),
      MachineItem::op(0x60, "П->X 0"), // proves moved-return X2 convergence
      MachineItem::op(0x54, "К НОП"),

      // Divergent call C1: it must return before Q in the final layout.
      MachineItem::op(0x53, "ПП"),
      MachineItem::address(helper),
      MachineItem::op(0x37, "К ∧"),
      MachineItem::op(0x61, "П->X 1"),
  };
  while (cell_count(items) < 41)
    items.push_back(MachineItem::op(0x54, "К НОП"));

  // Ordinary call C2 is a true tail call.  Once A's earlier Q is removed,
  // this two-cell call hole starts at 39 and can become the natural entry.
  items.push_back(MachineItem::op(0x53, "ПП"));     // 41
  items.push_back(MachineItem::address(helper));    // 42
  items.push_back(MachineItem::op(0x38, "К ∨"));    // 43
  items.push_back(MachineItem::op(0x49, "X->П 9")); // 44
  items.push_back(MachineItem::op(0x52, "В/О"));    // 45, tail continuation
  while (cell_count(items) < 60)
    items.push_back(MachineItem::op(0x50, "С/П"));

  items.push_back(MachineItem::label(helper));
  for (int cell = 0; cell < 9; ++cell)
    items.push_back(MachineItem::op(0x31, "К |x|")); // 60..68
  items.push_back(MachineItem::op(0x52, "В/О"));     // 69
  return items;
}

bool contains_reason(const std::vector<std::string>& reasons, std::string_view needle) {
  return std::any_of(reasons.begin(), reasons.end(), [&](const std::string& reason) {
    return reason.find(needle) != std::string::npos;
  });
}

std::vector<MachineItem> renamed_fixture() {
  std::vector<MachineItem> items = fixture("alpha_kernel");
  return items;
}

} // namespace

void natural_component_layout_rewrites_only_proved_shared_continuations() {
  const std::vector<MachineItem> baseline = fixture();
  const core::SharedHelperContinuationProof shared =
      core::verify_shared_helper_continuation(baseline, "value_kernel");
  require(shared.proved && shared.reasons.empty() && shared.calls.size() == 3U &&
              shared.ordinary_call_item_indices.size() == 2U && shared.helper_body_cells == 9 &&
              shared.join_opcode == 0x38 && shared.store_opcode == 0x49,
          "generic B proof should classify two equal Q continuations and one divergent call");

  const std::size_t natural_call = item_at_address(baseline, 41);
  const core::NaturalComponentLayoutProof layout =
      core::verify_natural_component_layout(baseline, "value_kernel", natural_call);
  require(layout.proved && layout.reasons.empty() && layout.relocated_helper_start_address == 39 &&
              layout.relocated_helper_body_end_address == 47 &&
              layout.relocated_continuation_start_address == 48 &&
              layout.relocated_return_address == 50,
          "generic C proof should derive the F9 boundary only from body length and call-hole "
          "layout");

  const core::NaturalComponentLayoutResult rewritten =
      core::rewrite_natural_component_layout(baseline, "value_kernel", natural_call);
  require(rewritten.applied == 1 && rewritten.proof.proved &&
              rewritten.proof.final_artifact_proved && rewritten.removed_cells == 5 &&
              cell_count(rewritten.items) == cell_count(baseline) - 5,
          "transactional B+C rewrite should share Q and naturalize the tail call by five cells");
  require(label_address(rewritten.items, "value_kernel") == 39 &&
              rewritten.items.at(item_at_address(rewritten.items, 47)).opcode == 0x31 &&
              rewritten.items.at(item_at_address(rewritten.items, 48)).opcode == 0x38 &&
              rewritten.items.at(item_at_address(rewritten.items, 49)).opcode == 0x49 &&
              rewritten.items.at(item_at_address(rewritten.items, 50)).opcode == 0x52,
          "final helper body/Q/return should occupy physical 39..50");

  int ordinary = 0;
  int divergent = 0;
  for (const MachineItem& item : rewritten.items) {
    if (item.kind != MachineItemKind::Address)
      continue;
    const auto* label = std::get_if<std::string>(&item.target);
    if (label == nullptr || *label != "value_kernel")
      continue;
    if (item.formal_opcode == 0xeb)
      ++divergent;
    else if (!item.formal_opcode.has_value())
      ++ordinary;
  }
  require(ordinary == 1 && divergent == 1,
          "one ordinary call should execute H+Q while one EB call returns after H");

  // Alpha-renaming cannot affect discovery or physical layout.
  {
    const std::vector<MachineItem> alpha = renamed_fixture();
    const core::SharedHelperContinuationProof alpha_shared =
        core::find_shared_helper_continuation(alpha);
    require(alpha_shared.proved && alpha_shared.helper_label == "alpha_kernel",
            "B discovery should be independent of helper spelling");
    const core::NaturalComponentLayoutResult alpha_result =
        core::rewrite_natural_component_layout(alpha, "alpha_kernel", item_at_address(alpha, 41));
    require(alpha_result.applied == 1 && alpha_result.removed_cells == 5 &&
                label_address(alpha_result.items, "alpha_kernel") == 39,
            "alpha-renamed fixture should retain the same generic delta");
  }

  // A complete unrelated indirect edge is accepted and reindexed by cell
  // identity; missing or continuation-entering target facts fail closed.
  {
    std::vector<MachineItem> indirect = fixture();
    const std::size_t flow = item_at_address(indirect, 20);
    indirect.at(flow) = MachineItem::op(0x87, "К БП 7");
    core::NaturalComponentLayoutOptions options;
    options.continuation_options.proved_indirect_flow_targets[flow] = {30};
    const core::NaturalComponentLayoutResult accepted = core::rewrite_natural_component_layout(
        indirect, "value_kernel", item_at_address(indirect, 41), options);
    require(accepted.applied == 1 &&
                std::any_of(accepted.proof.final_indirect_flow_targets.begin(),
                            accepted.proof.final_indirect_flow_targets.end(),
                            [](const auto& edge) { return edge.second == std::vector<int>{28}; }),
            "complete unrelated indirect target should follow the two-cell earlier Q deletion");

    const core::SharedHelperContinuationProof unknown =
        core::verify_shared_helper_continuation(indirect, "value_kernel");
    require(!unknown.proved && contains_reason(unknown.reasons, "complete target map"),
            "unknown indirect flow must reject B");

    core::SharedHelperContinuationOptions entering;
    entering.proved_indirect_flow_targets[flow] = {4};
    const core::SharedHelperContinuationProof entered =
        core::verify_shared_helper_continuation(indirect, "value_kernel", entering);
    require(!entered.proved && contains_reason(entered.reasons, "removable continuation"),
            "proved indirect entry into Q must reject B");
  }

  // Perturbation negatives: each failed obligation preserves the input.
  {
    std::vector<MachineItem> different_store = fixture();
    different_store.at(item_at_address(different_store, 44)) = MachineItem::op(0x48, "X->П 8");
    const auto rejected = core::verify_shared_helper_continuation(different_store, "value_kernel");
    require(!rejected.proved && contains_reason(rejected.reasons, "no unique pair"),
            "different destination stores must not be treated as identical Q");
  }
  {
    std::vector<MachineItem> x2_restore = fixture();
    x2_restore.at(item_at_address(x2_restore, 6)) = MachineItem::op(0x0a, ".");
    const auto rejected = core::verify_shared_helper_continuation(x2_restore, "value_kernel");
    require(!rejected.proved && contains_reason(rejected.reasons, "X2 convergence"),
            "an X2 restore before convergence must reject continuation motion");
  }
  {
    std::vector<MachineItem> second_entry = fixture();
    second_entry.insert(second_entry.begin() +
                            static_cast<std::ptrdiff_t>(item_at_address(second_entry, 64)),
                        MachineItem::label("second_entry"));
    const auto rejected = core::verify_shared_helper_continuation(second_entry, "value_kernel");
    require(!rejected.proved && contains_reason(rejected.reasons, "secondary executable entry"),
            "a second helper entry must reject B");
  }
  {
    std::vector<MachineItem> no_zero_return = fixture();
    no_zero_return.at(item_at_address(no_zero_return, 0)) = MachineItem::op(0x00, "0");
    const core::NaturalComponentLayoutResult rejected = core::rewrite_natural_component_layout(
        no_zero_return, "value_kernel", item_at_address(no_zero_return, 41));
    require(rejected.applied == 0 && rejected.items.size() == no_zero_return.size() &&
                rejected.items.at(item_at_address(rejected.items, 0)).opcode == 0x00 &&
                contains_reason(rejected.proof.reasons, "physical 00"),
            "missing return@00 must reject C atomically");
  }
  {
    std::vector<MachineItem> wrong_hole = fixture();
    wrong_hole.erase(wrong_hole.begin() +
                     static_cast<std::ptrdiff_t>(item_at_address(wrong_hole, 40)));
    const core::NaturalComponentLayoutResult rejected = core::rewrite_natural_component_layout(
        wrong_hole, "value_kernel", item_at_address(wrong_hole, 40));
    require(rejected.applied == 0 && rejected.items.size() == wrong_hole.size() &&
                cell_count(rejected.items) == cell_count(wrong_hole) &&
                contains_reason(rejected.proof.reasons, "physical 47"),
            "a call hole one cell early must reject C atomically");
  }
  {
    std::vector<MachineItem> non_tail = fixture();
    non_tail.at(item_at_address(non_tail, 45)) = MachineItem::op(0x50, "С/П");
    const core::NaturalComponentLayoutResult rejected = core::rewrite_natural_component_layout(
        non_tail, "value_kernel", item_at_address(non_tail, 41));
    require(rejected.applied == 0 && rejected.items.size() == non_tail.size() &&
                rejected.items.at(item_at_address(rejected.items, 45)).opcode == 0x50 &&
                contains_reason(rejected.proof.reasons, "terminal position"),
            "non-tail ordinary call must reject naturalization");
  }
}

} // namespace mkpro::tests
