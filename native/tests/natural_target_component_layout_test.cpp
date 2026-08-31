#include "mkpro/core/natural_target_component_layout.hpp"

#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/emit/lowering/proc_raw_setup.hpp"
#include "mkpro/core/indirect_addressing.hpp"
#include "mkpro/core/late_bound_decimal_selector.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::tests {

namespace {

MachineItem op(int opcode) {
  return MachineItem::op(opcode, opcode_by_code(opcode).name);
}

MachineItem stop() {
  MachineItem item = op(0x50);
  item.stop_disposition = StopDisposition::Terminal;
  return item;
}

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(), [](const MachineItem& item) {
    return item.kind != MachineItemKind::Label;
  }));
}

std::size_t item_at_address(const std::vector<MachineItem>& items, int wanted) {
  int address = 0;
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (items.at(item_index).kind == MachineItemKind::Label)
      continue;
    if (address == wanted)
      return item_index;
    ++address;
  }
  throw std::runtime_error("missing synthetic address " + std::to_string(wanted));
}

struct Fixture {
  std::vector<MachineItem> items;
  std::vector<PreloadReport> preloads;
  std::size_t visible_stop = 0;
  int selector_target = 34;
  std::string old_fractional_selector;
};

Fixture fixture(int calls, int padding_components, bool fixed_call_selector,
                int selector_target = 34) {
  require(calls >= 1 && padding_components >= 1,
          "synthetic fixture dimensions should be positive");
  Fixture result;
  result.selector_target = selector_target;
  const std::string helper = "opaque_leaf_" + std::to_string(calls);
  const std::string side_branch = "unrelated_side_" + std::to_string(calls);
  const std::string side_sink = "unrelated_sink_" + std::to_string(calls);

  result.items.push_back(MachineItem::label("opaque_entry_" + std::to_string(calls)));
  for (int call = 0; call < calls; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  result.items.push_back(op(0x6b)); // unrelated selector also has a data projection
  result.items.push_back(op(0x35));
  result.items.push_back(op(0x10));
  if (fixed_call_selector) {
    result.items.push_back(op(0x68)); // chosen call selector remains ordinary data
    result.items.push_back(op(0x35));
    result.items.push_back(op(0x10));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(side_branch));
  result.items.push_back(op(0x8b));
  result.items.back().indirect_flow_targets = std::vector<IrTarget>{side_sink};
  result.items.push_back(MachineItem::label(side_sink));
  result.items.push_back(stop());

  const int converted_main = calls + (fixed_call_selector ? 7 : 4);
  const int padding_cells = result.selector_target - converted_main - 2;
  require(padding_cells >= padding_components,
          "synthetic target should leave room for every padding component");
  int remaining = padding_cells;
  for (int component = 0; component < padding_components; ++component) {
    result.items.push_back(MachineItem::label(
        "opaque_padding_" + std::to_string(calls) + "_" + std::to_string(component)));
    const int components_left = padding_components - component;
    const int length = remaining / components_left;
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x0d));
    result.items.push_back(stop());
    remaining -= length;
  }
  require(remaining == 0, "synthetic padding partition should be exact");

  const int old_sink = 2 * calls + (fixed_call_selector ? 10 : 7);
  result.old_fractional_selector = std::to_string(old_sink) + ".375";
  result.preloads.push_back(PreloadReport{
      .register_name = "b",
      .value = result.old_fractional_selector,
  });
  if (fixed_call_selector) {
    result.preloads.push_back(PreloadReport{
        .register_name = "8",
        .value = std::to_string(result.selector_target),
    });
  }
  return result;
}

Fixture multi_anchor_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_multi_helper_a";
  const std::string helper_b = "unrelated_multi_helper_b";

  result.items.push_back(MachineItem::label("unrelated_multi_entry"));
  for (int call = 0; call < 2; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(helper_a));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(helper_b));
  result.items.push_back(op(0x23));
  result.items.push_back(op(0x24));
  result.items.push_back(op(0x52));

  const auto append_padding = [&](const std::string& name, int length) {
    result.items.push_back(MachineItem::label(name));
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x0d));
    result.items.push_back(stop());
  };
  append_padding("unrelated_multi_padding_14", 14);
  append_padding("unrelated_multi_padding_8", 8);

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "30"});
  result.preloads.push_back(PreloadReport{.register_name = "a", .value = "40"});
  return result;
}

Fixture paid_alignment_fixture() {
  Fixture result;
  const std::string helper = "unrelated_paid_alignment_helper";

  result.items.push_back(MachineItem::label("unrelated_paid_alignment_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label("unrelated_paid_alignment_padding"));
  for (int cell = 1; cell < 15; ++cell)
    result.items.push_back(op(0x0d));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  return result;
}

Fixture overlapping_fixed_targets_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_overlap_helper_a";
  const std::string helper_b = "unrelated_overlap_helper_b";

  result.items.push_back(MachineItem::label("unrelated_overlap_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  const auto append_helper = [&](const std::string& name, int first_opcode) {
    result.items.push_back(MachineItem::label(name));
    result.items.push_back(op(first_opcode));
    for (int cell = 0; cell < 8; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(op(0x52));
  };
  append_helper(helper_a, 0x22);
  append_helper(helper_b, 0x23);

  const auto append_padding = [&](const std::string& name, int length) {
    result.items.push_back(MachineItem::label(name));
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(stop());
  };
  append_padding("unrelated_overlap_padding_12", 12);
  append_padding("unrelated_overlap_padding_2", 2);

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "22"});
  return result;
}

Fixture split_overlapping_fixed_targets_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_split_helper_a";
  const std::string helper_b = "unrelated_split_helper_b";

  result.items.push_back(MachineItem::label("unrelated_split_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  for (int cell = 0; cell < 14; ++cell)
    result.items.push_back(op(0x54));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  const auto append_helper = [&](const std::string& name, int first_opcode, int length) {
    result.items.push_back(MachineItem::label(name));
    result.items.push_back(op(first_opcode));
    for (int cell = 2; cell < length; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(op(0x52));
  };
  append_helper(helper_a, 0x22, 10);
  append_helper(helper_b, 0x23, 7);

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "29"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "34"});
  return result;
}

Fixture split_internal_fixed_target_fixture() {
  Fixture result;
  const std::string helper_a = "unrelated_internal_split_helper_a";
  const std::string helper_b = "unrelated_internal_split_helper_b";

  result.items.push_back(MachineItem::label("unrelated_internal_split_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_a));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_b));
  }
  for (int cell = 0; cell < 12; ++cell)
    result.items.push_back(op(0x54));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(op(0x54));
  result.items.push_back(op(0x54));
  result.items.push_back(MachineItem::label(helper_a));
  result.items.push_back(op(0x22));
  for (int cell = 2; cell < 10; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(helper_b));
  result.items.push_back(op(0x23));
  for (int cell = 2; cell < 7; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(op(0x52));

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "29"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "34"});
  return result;
}

Fixture shared_call_and_tail_jump_fixture() {
  Fixture result;
  const std::string helper = "unrelated_shared_flow_helper";

  result.items.push_back(MachineItem::label("unrelated_shared_flow_entry"));
  for (int call = 0; call < 2; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_shared_flow_tail"));
  result.items.push_back(op(0x51));
  result.items.push_back(MachineItem::address(helper));

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label("unrelated_shared_flow_padding"));
  for (int cell = 1; cell < 16; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  return result;
}

Fixture indirect_split_bridge_fixture() {
  Fixture result;
  const std::string prefix = "unrelated_indirect_split_prefix";
  const std::string suffix = "unrelated_indirect_split_suffix";

  result.items.push_back(MachineItem::label("unrelated_indirect_split_entry"));
  for (int call = 0; call < 2; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(prefix));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(suffix));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_indirect_split_padding"));
  for (int cell = 1; cell < 13; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(prefix));
  for (int cell = 0; cell < 10; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(MachineItem::label(suffix));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "31"});
  return result;
}

Fixture indirect_split_bridge_call_continuation_fixture() {
  Fixture result;
  const std::string prefix = "unrelated_split_call_prefix";
  const std::string suffix = "unrelated_split_call_suffix";
  const std::string callee = "unrelated_split_call_callee";

  result.items.push_back(MachineItem::label("unrelated_split_call_entry"));
  for (int call = 0; call < 2; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(prefix));
  }
  for (int call = 0; call < 4; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(suffix));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_split_call_padding"));
  for (int cell = 1; cell < 13; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(prefix));
  for (int cell = 0; cell < 8; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(op(0x53));
  result.items.push_back(MachineItem::address(callee));
  result.items.push_back(MachineItem::label(suffix));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(callee));
  result.items.push_back(op(0x54));
  result.items.push_back(op(0x52));

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "31"});
  return result;
}

Fixture shared_conditional_fixture() {
  Fixture result;
  const std::string guard_sink = "unrelated_shared_conditional_guard_sink";
  const std::string sink = "unrelated_shared_conditional_sink";

  result.items.push_back(MachineItem::label("unrelated_shared_conditional_entry"));
  result.items.push_back(op(0x59));
  result.items.push_back(MachineItem::address(guard_sink));
  result.items.push_back(op(0x5e));
  result.items.push_back(MachineItem::address(sink));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(guard_sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_shared_conditional_padding"));
  for (int cell = 1; cell < 16; ++cell)
    result.items.push_back(op(0x0d));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "20"});
  return result;
}

Fixture bounded_direct_gap_split_fixture() {
  Fixture result;
  const std::string helper_99 = "bounded_gap_helper_99";
  const std::string helper_34 = "bounded_gap_helper_34";
  const std::string helper_76 = "bounded_gap_helper_76";

  result.items.push_back(MachineItem::label("bounded_gap_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_99));
  }
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_34));
  }
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper_76));
  }
  for (int cell = 0; cell < 10; ++cell)
    result.items.push_back(op(0x54));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  const auto append_helper = [&](const std::string& name, int length) {
    result.items.push_back(MachineItem::label(name));
    for (int cell = 1; cell < length; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(op(0x52));
  };
  append_helper(helper_99, 18);
  append_helper(helper_34, 9);
  append_helper(helper_76, 13);

  const auto append_free_segment = [&](const std::string& name, int length,
                                       int bounded_offset) {
    result.items.push_back(MachineItem::label(name));
    for (int cell = 0; cell < length - 1; ++cell) {
      if (cell == bounded_offset)
        result.items.push_back(MachineItem::label("bounded_gap_target"));
      result.items.push_back(op(0x54));
    }
    result.items.push_back(stop());
  };
  append_free_segment("bounded_gap_free_36", 36, 23);
  append_free_segment("bounded_gap_free_11", 11, -1);
  append_free_segment("bounded_gap_free_2", 2, -1);
  append_free_segment("bounded_gap_free_8", 8, -1);
  append_free_segment("bounded_gap_free_18", 18, -1);

  result.preloads.push_back(PreloadReport{.register_name = "a", .value = "99"});
  result.preloads.push_back(PreloadReport{.register_name = "c", .value = "34"});
  result.preloads.push_back(PreloadReport{.register_name = "d", .value = "76"});
  return result;
}

Fixture conditional_x2_reconvergence_fixture(bool restore_before_overwrite) {
  Fixture result;
  const std::string sink = "unrelated_x2_reconvergence_sink";

  result.items.push_back(MachineItem::label("unrelated_x2_reconvergence_entry"));
  for (int branch = 0; branch < 2; ++branch) {
    result.items.push_back(op(0x60));
    result.items.push_back(op(0x5e));
    result.items.push_back(MachineItem::address(sink));
    if (restore_before_overwrite)
      result.items.push_back(op(0x0a));
    result.items.push_back(op(0x61));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_x2_reconvergence_padding"));
  const int padding_cells = restore_before_overwrite ? 9 : 11;
  for (int cell = 1; cell < padding_cells; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "18"});
  return result;
}

Fixture single_flow_conditional_x2_reconvergence_fixture() {
  Fixture result;
  const std::string sink = "unrelated_single_flow_x2_sink";

  result.items.push_back(MachineItem::label("unrelated_single_flow_x2_entry"));
  result.items.push_back(op(0x60));
  result.items.push_back(op(0x5e));
  result.items.push_back(MachineItem::address(sink));
  result.items.push_back(op(0x61));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_single_flow_x2_padding"));
  for (int cell = 1; cell < 14; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "18"});
  return result;
}

Fixture loop_conditional_x2_reconvergence_fixture(bool restore_inside_loop) {
  Fixture result;
  const std::string loop = "unrelated_loop_x2_body";
  const std::string sink = "unrelated_loop_x2_sink";

  result.items.push_back(MachineItem::label("unrelated_loop_x2_entry"));
  result.items.push_back(op(0x5e));
  result.items.push_back(MachineItem::address(sink));
  result.items.push_back(MachineItem::label(loop));
  if (restore_inside_loop)
    result.items.push_back(op(0x0a));
  result.items.push_back(op(0x5b));
  result.items.push_back(MachineItem::address(loop));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_loop_x2_padding"));
  for (int cell = 1; cell < 14; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "1", .value = "2"});
  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "18"});
  return result;
}

// A stable register written only by the exact toggle triple `П->X r; /-/;
// X->П r`. `write` selects the runtime write shape: 0 keeps the proved
// triple, 1 stores without the negate, 2 places a label between the negate
// and the store so the triple is no longer uninterruptible.
Fixture sign_toggle_selector_fixture(const std::string& preload_value,
                                     int padding_cells, int write = 0) {
  Fixture result;
  const std::string sink = "sign_toggle_sink";

  result.items.push_back(MachineItem::label("sign_toggle_entry"));
  result.items.push_back(op(0x68)); // П->X 8
  if (write == 0 || write == 2)
    result.items.push_back(op(0x0b)); // /-/
  if (write == 2)
    result.items.push_back(MachineItem::label("sign_toggle_interrupt"));
  result.items.push_back(op(0x48)); // X->П 8
  result.items.push_back(op(0x51)); // БП
  result.items.push_back(MachineItem::address(sink));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  if (padding_cells > 0) {
    result.items.push_back(MachineItem::label("sign_toggle_padding"));
    for (int cell = 1; cell < padding_cells; ++cell)
      result.items.push_back(op(0x54));
    result.items.push_back(stop());
  }

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = preload_value});
  return result;
}

Fixture conditional_x2_value_equality_fixture() {
  Fixture result;
  const std::string sink = "unrelated_x2_value_equality_sink";

  result.items.push_back(MachineItem::label("unrelated_x2_value_equality_entry"));
  for (int branch = 0; branch < 2; ++branch) {
    result.items.push_back(op(0x60));
    result.items.push_back(op(0x60));
    result.items.push_back(op(0x5e));
    result.items.push_back(MachineItem::address(sink));
    result.items.push_back(op(0x05));
  }
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("unrelated_x2_value_equality_padding"));
  for (int cell = 1; cell < 9; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "18"});
  return result;
}

Fixture late_runtime_selector_fixture(bool marked_store,
                                      bool observe_x2_before_overwrite) {
  Fixture result;
  const std::string old_leaf = "late_runtime_old_leaf";
  const std::string shared_sink = "late_runtime_shared_sink";
  const std::string selector_role = "late-decimal-selector-register:8";

  result.items.push_back(MachineItem::label("late_runtime_entry"));
  for (const core::LateBoundDecimalSelectorPart part :
       {core::LateBoundDecimalSelectorPart::High,
        core::LateBoundDecimalSelectorPart::Low}) {
    MachineItem digit = op(0x00);
    digit.roles.push_back(
        core::make_late_bound_decimal_selector_role(part, old_leaf));
    digit.roles.push_back(selector_role);
    result.items.push_back(std::move(digit));
  }
  MachineItem store = op(0x48);
  if (marked_store)
    store.roles.push_back("late-decimal-selector-store");
  result.items.push_back(std::move(store));
  MachineItem old_call = op(0xa8);
  old_call.roles.push_back("late-decimal-selector-consumer");
  old_call.indirect_flow_targets = std::vector<IrTarget>{old_leaf};
  result.items.push_back(std::move(old_call));
  result.items.push_back(op(0x20));

  for (int branch = 0; branch < 2; ++branch) {
    result.items.push_back(op(observe_x2_before_overwrite ? 0x20 : 0x60));
    result.items.push_back(op(0x5e));
    result.items.push_back(MachineItem::address(shared_sink));
    if (observe_x2_before_overwrite)
      result.items.push_back(op(0x0a));
    result.items.push_back(op(0x61));
  }
  result.items.push_back(op(0x00));
  MachineItem main_cleanup = op(0x48);
  main_cleanup.roles.push_back("late-decimal-selector-store");
  result.items.push_back(std::move(main_cleanup));
  result.items.push_back(op(0x20));
  result.items.push_back(op(0x61));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(shared_sink));
  result.items.push_back(op(0x00));
  MachineItem sink_cleanup = op(0x48);
  sink_cleanup.roles.push_back("late-decimal-selector-store");
  result.items.push_back(std::move(sink_cleanup));
  result.items.push_back(op(0x20));
  result.items.push_back(op(0x61));
  result.items.push_back(stop());
  result.items.push_back(MachineItem::label(old_leaf));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));
  result.items.push_back(MachineItem::label("late_runtime_padding"));
  for (int cell = 1; cell < 12; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "0", .value = "0"});
  result.preloads.push_back(PreloadReport{.register_name = "1", .value = "1"});
  core::LateBoundDecimalSelectorOptions bind_options;
  bind_options.minimum_target_address = 0;
  const core::LateBoundDecimalSelectorResult bound =
      core::bind_late_bound_decimal_selectors(result.items, bind_options);
  require(bound.diagnostics.empty() && bound.applied == 1,
          "synthetic late selector charge should bind before component layout");
  result.items = bound.items;
  return result;
}

Fixture address_selector_rebind_fixture() {
  Fixture result;
  const std::string helper = "unrelated_rebound_address_helper";
  const std::string sink = "unrelated_rebound_address_sink";

  result.items.push_back(MachineItem::label("unrelated_rebound_address_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  MachineItem old_indirect_jump = op(0x8e);
  old_indirect_jump.indirect_flow_targets = std::vector<IrTarget>{sink};
  result.items.push_back(std::move(old_indirect_jump));

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label(sink));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());
  result.preloads.push_back(PreloadReport{.register_name = "e", .value = "9"});
  return result;
}

Fixture companion_address_selector_rebind_fixture(bool data_visible = false) {
  Fixture result;
  const std::string helper = "companion_rebind_helper";
  const std::string sink = "companion_rebind_sink";

  result.items.push_back(MachineItem::label("companion_rebind_entry"));
  for (int call = 0; call < 3; ++call) {
    result.items.push_back(op(0x53));
    result.items.push_back(MachineItem::address(helper));
  }
  if (data_visible)
    result.items.push_back(op(0x69));
  MachineItem existing_jump = op(0x89);
  existing_jump.indirect_flow_targets = std::vector<IrTarget>{sink};
  result.items.push_back(std::move(existing_jump));

  result.items.push_back(MachineItem::label(helper));
  result.items.push_back(op(0x22));
  result.items.push_back(op(0x52));

  result.items.push_back(MachineItem::label("companion_rebind_before_sink"));
  const int before_sink_cells = data_visible ? 5 : 6;
  for (int cell = 1; cell < before_sink_cells; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label(sink));
  result.visible_stop = result.items.size();
  result.items.push_back(stop());

  result.items.push_back(MachineItem::label("companion_rebind_tail"));
  for (int cell = 1; cell < 13; ++cell)
    result.items.push_back(op(0x54));
  result.items.push_back(stop());

  result.preloads.push_back(PreloadReport{.register_name = "8", .value = "18"});
  result.preloads.push_back(PreloadReport{.register_name = "9", .value = "C7"});
  return result;
}

core::AuthoritativePostLayoutControlFlow flow(const Fixture& fixture_value) {
  return core::build_post_layout_control_flow(fixture_value.items);
}

std::string trim_ascii(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    text.erase(text.begin());
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    text.pop_back();
  return text;
}

std::string canonical_register(std::string text) {
  text = trim_ascii(std::move(text));
  const std::size_t sign = !text.empty() && text.front() == '-' ? 1U : 0U;
  while (text.size() > sign + 1U && text.at(sign) == '0' &&
         std::isdigit(static_cast<unsigned char>(text.at(sign + 1U))) != 0) {
    text.erase(sign, 1U);
  }
  return text;
}

std::map<std::string, std::string> preload_map(const std::vector<PreloadReport>& preloads) {
  std::map<std::string, std::string> result;
  for (const PreloadReport& preload : preloads)
    result.emplace(preload.register_name, preload.value);
  return result;
}

struct Observation {
  bool stopped = false;
  std::map<std::string, std::string> state;
};

Observation observe(const std::vector<MachineItem>& items,
                    const std::vector<PreloadReport>& preloads,
                    bool generated_setup = false) {
  const ResolvedProgram resolved = resolve_machine_items(items, {});
  require(resolved.diagnostics.empty(), "synthetic layout should resolve to official opcodes");
  std::vector<int> codes;
  for (const ResolvedStep& step : resolved.steps)
    codes.push_back(step.opcode);
  emulator::MK61 calc;
  if (generated_setup) {
    const SetupProgramReport setup =
        core::emit::lowering::compile_setup_program_with_preloads(
            {}, {}, preloads, CompileOptions{});
    std::vector<int> setup_codes;
    for (const ResolvedStep& step : setup.steps)
      setup_codes.push_back(step.opcode);
    require(calc.load_program(setup_codes).diagnostics.empty(),
            "generated synthetic setup should load in the emulator");
    calc.press_sequence({"В/О", "С/П"});
    require(calc.run_until_stable(1000, 6).stopped,
            "generated synthetic setup should stop");
  }
  require(calc.load_program(codes).diagnostics.empty(),
          "synthetic layout should load in the emulator");
  calc.set_register("x", "2");
  calc.set_register("y", "0");
  calc.set_register("z", "0");
  calc.set_register("t", "0");
  calc.set_register("x1", "0");
  for (const PreloadReport& preload : preloads) {
    if (generated_setup)
      continue;
    calc.set_register(preload.register_name, preload.value);
  }
  calc.press_sequence({"В/О", "С/П"});
  const emulator::RunResult run = calc.run_until_stable(1000, 6);
  Observation result{.stopped = run.stopped};
  for (const std::string& slot : {"x", "y", "z", "t", "x1", "8", "9"})
    result.state.emplace(slot, canonical_register(calc.read_register(slot)));
  return result;
}

bool reason_contains(const core::NaturalTargetComponentLayoutPlan& plan,
                     const std::string& fragment) {
  return std::any_of(plan.reasons.begin(), plan.reasons.end(), [&](const std::string& reason) {
    return reason.find(fragment) != std::string::npos;
  });
}

} // namespace

void natural_target_component_layout_is_generic_and_proof_gated() {
  {
    const Fixture input = late_runtime_selector_fixture(true, false);
    core::NaturalTargetComponentLayoutOptions options;
    options.maximum_anchors = 1;
    options.enable_late_bound_runtime_selector_composition = true;
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(),
        [](const core::NaturalTargetFlowRewrite& rewrite) {
          return rewrite.original_opcode == 0x5e &&
                 rewrite.selector_register == "8";
        }));
    std::string late_diagnostics;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!late_diagnostics.empty())
        late_diagnostics += " | ";
      late_diagnostics += reason;
    }
    require(rewritten.plan.proved && rewritten.applied > 0 &&
                rewritten.removed_cells == 1 && converted_conditionals == 2 &&
                rewritten.plan.rebound_indirect_flows == 1 &&
                std::count_if(rewritten.plan.runtime_selectors.begin(),
                              rewritten.plan.runtime_selectors.end(),
                              [](const auto& proof) {
                                return proof.register_name == "8" &&
                                       proof.typed_target_matches_runtime_decode;
                              }) >= 2,
            "a marked late runtime selector should be reassigned only when two generic "
            "flows repay its displaced old consumer: proved=" +
                std::to_string(rewritten.plan.proved) + " applied=" +
                std::to_string(rewritten.applied) + " removed=" +
                std::to_string(rewritten.removed_cells) + " conditionals=" +
                std::to_string(converted_conditionals) + " displaced=" +
                std::to_string(rewritten.plan.rebound_indirect_flows) +
                " reasons=" + late_diagnostics);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "late runtime selector reassignment must preserve emulator-visible state: "
            "before=" + before.state.at("x") + "/" + before.state.at("y") + "/" +
                before.state.at("z") + "/" + before.state.at("t") + "/" +
                before.state.at("x1") + "/" + before.state.at("8") + "/" +
                before.state.at("9") + " after=" + after.state.at("x") + "/" +
                after.state.at("y") + "/" + after.state.at("z") + "/" +
                after.state.at("t") + "/" + after.state.at("x1") + "/" +
                after.state.at("8") + "/" + after.state.at("9"));
  }

  {
    const Fixture unmarked = late_runtime_selector_fixture(false, false);
    core::NaturalTargetComponentLayoutOptions runtime_options;
    runtime_options.enable_late_bound_runtime_selector_composition = true;
    const auto rejected = core::optimize_natural_target_component_layout(
        unmarked.items, unmarked.preloads, flow(unmarked), runtime_options);
    require(!rejected.plan.proved && rejected.applied == 0,
            "an unmarked runtime store must not become a selector candidate");

    const Fixture x2_observed = late_runtime_selector_fixture(true, true);
    const auto x2_rejected = core::optimize_natural_target_component_layout(
        x2_observed.items, x2_observed.preloads, flow(x2_observed),
        runtime_options);
    const bool used_runtime_selector = std::any_of(
        x2_rejected.plan.flows.begin(), x2_rejected.plan.flows.end(),
        [](const core::NaturalTargetFlowRewrite& rewrite) {
          return rewrite.selector_register == "8";
        });
    require(!used_runtime_selector &&
                x2_rejected.plan.rebound_indirect_flows == 0,
            "late runtime selector reassignment must fail when conditional X2 is observed: "
            "proved=" + std::to_string(x2_rejected.plan.proved) + " applied=" +
                std::to_string(x2_rejected.applied) + " flows=" +
                std::to_string(x2_rejected.plan.flows.size()) + " rebound=" +
                std::to_string(x2_rejected.plan.rebound_indirect_flows) + " x2=" +
                std::to_string(x2_rejected.plan.x2_reconvergence_flows));
  }

  {
    Fixture input;
    input.items.push_back(MachineItem::label("absolute_entry"));
    input.items.push_back(stop());
    input.items.push_back(MachineItem::label("absolute_leaf"));
    const std::size_t leaf_item = input.items.size();
    input.items.push_back(op(0x22));
    input.items.push_back(op(0x52));
    input.items.push_back(MachineItem::label("absolute_padding"));
    input.items.push_back(op(0x0d));
    input.items.push_back(op(0x0d));
    input.items.push_back(op(0x0d));
    input.items.push_back(stop());

    core::NaturalTargetComponentLayoutOptions options;
    options.required_absolute_targets.push_back(
        core::NaturalTargetRequiredAbsoluteTarget{
            .target_item = leaf_item,
            .target_address = 5,
        });
    options.allow_size_neutral_absolute_layout = true;
    options.require_size_neutral_absolute_layout = true;
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    require(rewritten.plan.proved && rewritten.applied > 0 &&
                rewritten.removed_cells == 0 &&
                rewritten.plan.size_neutral_absolute_layout &&
                rewritten.plan.absolute_targets == 1 &&
                rewritten.plan.absolute_targets_proved &&
                rewritten.items.at(item_at_address(rewritten.items, 5)).opcode == 0x22 &&
                cell_count(rewritten.items) == cell_count(input.items),
            "absolute target layout should prove an exact size-neutral component reorder");

    options.required_absolute_targets.front().target_address = 0;
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    require(!rejected.plan.proved && rejected.applied == 0,
            "absolute target layout should fail closed on a main-component conflict");
  }

  {
    Fixture input;
    input.items.push_back(MachineItem::label("bounded_entry"));
    input.items.push_back(stop());
    input.items.push_back(MachineItem::label("bounded_padding"));
    for (int cell = 0; cell < 5; ++cell)
      input.items.push_back(op(0x0d));
    input.items.push_back(stop());
    input.items.push_back(MachineItem::label("bounded_leaf"));
    input.items.push_back(op(0x22));
    input.items.push_back(op(0x52));

    core::NaturalTargetComponentLayoutOptions options;
    options.required_bounded_target_labels = {"bounded_leaf"};
    options.maximum_bounded_target_address = 3;
    options.allow_size_neutral_bounded_layout = true;
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    int bounded_leaf_address = -1;
    int address = 0;
    for (const MachineItem& item : rewritten.items) {
      if (item.kind == MachineItemKind::Label) {
        if (item.name == "bounded_leaf")
          bounded_leaf_address = address;
      } else {
        ++address;
      }
    }
    require(rewritten.plan.proved && rewritten.applied > 0 &&
                rewritten.removed_cells == 0 &&
                rewritten.plan.size_neutral_bounded_layout &&
                rewritten.plan.bounded_targets == 1 &&
                rewritten.plan.bounded_targets_proved && bounded_leaf_address >= 0 &&
                bounded_leaf_address <= 3 &&
                cell_count(rewritten.items) == cell_count(input.items),
            "bounded target layout should prove a size-neutral component reorder");

    options.maximum_bounded_target_address = 0;
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    require(!rejected.plan.proved && rejected.applied == 0,
            "bounded target layout should fail closed when main occupies the whole range");
  }

  {
    const Fixture input = bounded_direct_gap_split_fixture();
    core::NaturalTargetComponentLayoutOptions options;
    options.required_bounded_target_labels = {"bounded_gap_target"};
    options.maximum_bounded_target_address = 99;
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    int bounded_target_address = -1;
    int address = 0;
    for (const MachineItem& item : rewritten.items) {
      if (item.kind == MachineItemKind::Label) {
        if (item.name == "bounded_gap_target")
          bounded_target_address = address;
      } else {
        ++address;
      }
    }
    require(rewritten.plan.proved && rewritten.applied > 0 &&
                rewritten.removed_cells > 0 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.plan.bounded_targets == 1 &&
                rewritten.plan.bounded_targets_proved &&
                bounded_target_address >= 0 && bounded_target_address <= 99 &&
                cell_count(rewritten.items) < cell_count(input.items),
            "a profitable direct gap split should preserve and prove a bounded suffix target");
  }

  {
    const Fixture input = shared_conditional_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x5e;
        }));
    std::string rejection;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!rejection.empty())
        rejection += " | ";
      rejection += reason;
    }
    require(rewritten.plan.proved && rewritten.applied == 1 &&
                rewritten.removed_cells == 1 && converted_conditionals == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 2)).opcode == 0xe8 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x50,
            "compatible conditionals should share one proved natural-target selector: applied=" +
                std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", reasons=" + rejection);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "conditional flow conversion must preserve observable machine state");
  }

  {
    const Fixture input = conditional_x2_reconvergence_fixture(false);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x5e;
        }));
    std::string rejection;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!rejection.empty())
        rejection += " | ";
      rejection += reason;
    }
    require(rewritten.plan.proved && rewritten.removed_cells == 2 &&
                converted_conditionals == 2 &&
                rewritten.plan.x2_reconvergence_flows == 2,
            "two conditionals may share a selector when every changed fallthrough X2 "
            "value is overwritten before it can be observed: applied=" +
                std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", reasons=" + rejection);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "proved conditional X2 reconvergence must preserve observable state");
  }

  {
    const Fixture input = single_flow_conditional_x2_reconvergence_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x5e;
        }));
    std::string rejection;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!rejection.empty())
        rejection += " | ";
      rejection += reason;
    }
    require(rewritten.plan.proved && rewritten.removed_cells == 1 &&
                converted_conditionals == 1 &&
                rewritten.plan.x2_reconvergence_flows == 1,
            "a single conditional flow may use the reconvergence proof when its changed "
            "fallthrough X2 value is overwritten before it can be observed: applied=" +
                std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", reasons=" + rejection);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "proved single-flow conditional X2 reconvergence must preserve observable state");
  }

  {
    const Fixture input = loop_conditional_x2_reconvergence_fixture(false);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x5e;
        }));
    std::string rejection;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!rejection.empty())
        rejection += " | ";
      rejection += reason;
    }
    require(rewritten.plan.proved && rewritten.removed_cells == 1 &&
                converted_conditionals == 1 &&
                rewritten.plan.x2_reconvergence_flows == 1,
            "a preserving counted-loop SCC may carry a hidden conditional X2 difference "
            "until every exit overwrites it: applied=" +
                std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", reasons=" + rejection);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "loop-aware conditional X2 reconvergence must preserve observable state");
  }

  {
    const Fixture input = loop_conditional_x2_reconvergence_fixture(true);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.applied == 0 && reason_contains(rewritten.plan, "x2=0"),
            "an X2 restore reachable inside a preserving SCC must reject indirect "
            "conditional conversion");
  }

  {
    const Fixture input = sign_toggle_selector_fixture("99999918", 13);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    std::string rejection;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!rejection.empty())
        rejection += " | ";
      rejection += reason;
    }
    const bool converted_jump = std::any_of(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x51 && flow.selector_register == "8";
        });
    require(rewritten.plan.proved && rewritten.removed_cells == 1 && converted_jump,
            "a register written only by uninterruptible sign toggles with a sign-invariant "
            "decode may anchor a natural target: applied=" +
                std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", reasons=" + rejection);
    const bool sign_toggle_proof = std::any_of(
        rewritten.plan.runtime_selectors.begin(), rewritten.plan.runtime_selectors.end(),
        [](const core::NaturalTargetRuntimeSelectorProof& proof) {
          return proof.register_name == "8" && !proof.selector_unwritten &&
                 proof.selector_sign_toggle_invariant;
        });
    require(sign_toggle_proof,
            "the runtime selector proof must record the sign-toggle invariance instead of "
            "claiming the register is unwritten");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "a sign-toggled selector conversion must preserve observable machine state");
  }

  {
    const Fixture input = sign_toggle_selector_fixture("5", 5);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.applied == 0,
            "a sign-toggled single-digit preload decodes to different targets per sign and "
            "must not anchor a natural target");
  }

  {
    const Fixture input = sign_toggle_selector_fixture("18", 13);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.applied == 0,
            "a sign-toggled preload whose negative phase is not a fixed point of the "
            "selector write-back must not anchor a natural target");
  }

  {
    const Fixture input = sign_toggle_selector_fixture("18", 14, 1);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.applied == 0,
            "a store that is not part of an exact recall/negate/store triple must keep the "
            "register rejected as a selector");
  }

  {
    const Fixture input = sign_toggle_selector_fixture("18", 13, 2);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.applied == 0,
            "a label between the negate and the store makes the toggle interruptible and "
            "must keep the register rejected as a selector");
  }

  {
    const Fixture input = conditional_x2_reconvergence_fixture(true);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    if (rewritten.applied > 0) {
      const Observation before = observe(input.items, input.preloads);
      const Observation after = observe(rewritten.items, rewritten.preloads);
      require(before.stopped && after.stopped && before.state == after.state,
              "value equality must preserve a restored X2 before overwrite");
    } else {
      require(reason_contains(rewritten.plan, "x2=0"),
              "an X2 restore before overwrite must reject indirect conditional conversion");
    }
  }

  {
    const Fixture input = conditional_x2_value_equality_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_conditionals = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x5e;
        }));
    require(rewritten.plan.proved && rewritten.removed_cells == 2 &&
                converted_conditionals == 2,
            "conditionals may share a selector when value flow proves X2 equals X at every "
            "entry");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "value-proved conditional conversion must preserve observable machine state");
  }

  {
    const Fixture input = shared_call_and_tail_jump_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const int converted_calls = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x53;
        }));
    const int converted_jumps = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.original_opcode == 0x51;
        }));
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 3 && converted_calls == 2 &&
                converted_jumps == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 0)).opcode == 0xa8 &&
                rewritten.items.at(item_at_address(rewritten.items, 1)).opcode == 0xa8 &&
                rewritten.items.at(item_at_address(rewritten.items, 3)).opcode == 0x88 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x22,
            "calls and a tail jump should share one proved natural-target selector");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "mixed direct-flow conversion must preserve observable machine state");
  }

  {
    const Fixture input = indirect_split_bridge_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 6 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 30)).opcode == 0x89 &&
                rewritten.items.at(item_at_address(rewritten.items, 31)).opcode == 0x22,
            "a selected suffix target should split its fallthrough component with one "
            "proved indirect bridge");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "an indirect split bridge must preserve observable machine state");
  }

  {
    Fixture input = indirect_split_bridge_fixture();
    MachineItem donor = op(0x89);
    donor.indirect_flow_targets =
        std::vector<IrTarget>{std::string("unrelated_indirect_split_suffix")};
    input.items.at(item_at_address(input.items, 13)) = std::move(donor);
    for (int removed = 0; removed < 5; ++removed) {
      input.items.erase(input.items.begin() +
                        static_cast<std::ptrdiff_t>(
                            item_at_address(input.items, 14)));
    }
    input.items.push_back(op(0x69));
    for (int padding = 0; padding < 4; ++padding)
      input.items.push_back(op(0x54));
    input.items.push_back(stop());
    input.preloads.at(1).value = "31";
    core::NaturalTargetComponentLayoutOptions donor_options;
    int direct_call = 0;
    for (std::size_t item = 0; item < input.items.size(); ++item) {
      if (input.items.at(item).kind != MachineItemKind::Op ||
          input.items.at(item).opcode != 0x53) {
        continue;
      }
      donor_options.required_flow_selectors.push_back(
          core::NaturalTargetRequiredFlowSelector{
              .command_item = item,
              .register_name = direct_call < 2 ? "8" : "9",
          });
      ++direct_call;
    }
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), donor_options);
    std::string donor_detail =
        " applied=" + std::to_string(rewritten.applied) +
        " removed=" + std::to_string(rewritten.removed_cells) +
        " bridges=" +
        std::to_string(rewritten.plan.transparent_split_bridges) +
        " reused=" +
        std::to_string(rewritten.plan.reused_split_bridge_commands);
    for (const std::string& reason : rewritten.plan.reasons)
      donor_detail += "; " + reason;
    require(rewritten.plan.proved && rewritten.applied > 0 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.plan.reused_split_bridge_commands == 1,
            "a separately addressed equivalent indirect jump should donate its "
            "existing cell to a natural-target split bridge:" + donor_detail);
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "a donated split bridge must preserve observable machine state");

    input.items.at(item_at_address(input.items, 13)).opcode = 0x8c;
    input.items.at(item_at_address(input.items, 13)).mnemonic =
        opcode_by_code(0x8c).name;
    input.preloads.push_back(
        PreloadReport{.register_name = "c", .value = "31"});
    const auto mismatched = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), donor_options);
    require(mismatched.plan.reused_split_bridge_commands == 0,
            "a donor with a different selector must not be reused");
  }

  {
    const Fixture input = indirect_split_bridge_call_continuation_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied > 0 &&
                rewritten.plan.transparent_split_bridges == 1,
            "an indirect split bridge should remain transparent when a nested call "
            "returns through it");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "a split bridge must canonicalize dynamic return continuations");
  }

  {
    const Fixture input = split_internal_fixed_target_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 7 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_trampolines == 0 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 32)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 33)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 34)).kind ==
                    MachineItemKind::Op,
            "a fixed target inside a fallthrough component should admit a proved split");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "an internal-target split must preserve observable machine state");
  }

  {
    const Fixture input = split_overlapping_fixed_targets_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 7 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_trampolines == 0 &&
                rewritten.plan.transparent_split_bridges == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 32)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 33)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 34)).kind ==
                    MachineItemKind::Op,
            "a helper suffix should fill the gap before two overlapping fixed targets");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "transparent helper split must preserve observable machine state");
  }

  {
    const Fixture input = overlapping_fixed_targets_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 7 &&
                rewritten.removed_cells == 5 &&
                rewritten.plan.transparent_trampolines == 1 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 21)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 22)).kind ==
                    MachineItemKind::Op,
            "overlapping fixed targets should share a profitable proved jump trampoline");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "transparent natural-target trampoline must preserve observable machine state");
  }

  {
    const Fixture input = address_selector_rebind_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 2 &&
                rewritten.plan.rebound_indirect_flows == 1 &&
                final_preloads.at("e") == "5" &&
                rewritten.items.at(item_at_address(rewritten.items, 3)).opcode == 0x51 &&
                rewritten.items.at(item_at_address(rewritten.items, 4)).kind ==
                    MachineItemKind::Address &&
                rewritten.items.at(item_at_address(rewritten.items, 5)).opcode == 0x22,
            "a stable address-only selector should move from one flow to three calls");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "address-only selector reassignment must preserve observable machine state");

    Fixture data_visible = address_selector_rebind_fixture();
    const auto old_jump = std::find_if(
        data_visible.items.begin(), data_visible.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x8e;
        });
    require(old_jump != data_visible.items.end(),
            "address-only fixture should expose its old indirect jump");
    data_visible.items.insert(old_jump, op(0x6e));
    data_visible.preloads.front().value = "10";
    const auto rejected = core::optimize_natural_target_component_layout(
        data_visible.items, data_visible.preloads, flow(data_visible));
    require(rejected.plan.rebound_indirect_flows == 0,
            "a selector with an ordinary data recall must not be reassigned");
  }

  {
    Fixture input;
    const std::string helper = "same_target_rebound_helper";
    input.items.push_back(MachineItem::label("same_target_rebound_entry"));
    MachineItem existing_call = op(0xa9);
    existing_call.indirect_flow_targets = std::vector<IrTarget>{helper};
    input.items.push_back(std::move(existing_call));
    for (int call = 0; call < 3; ++call) {
      input.items.push_back(op(0x53));
      input.items.push_back(MachineItem::address(helper));
    }
    input.visible_stop = input.items.size();
    input.items.push_back(stop());
    for (int cell = 8; cell < 14; ++cell)
      input.items.push_back(op(0x54));
    input.items.push_back(stop());
    input.items.push_back(MachineItem::label(helper));
    const std::size_t helper_item = input.items.size();
    input.items.push_back(op(0x22));
    input.items.push_back(op(0x52));
    input.preloads.push_back(
        PreloadReport{.register_name = "9", .value = "C7"});

    core::NaturalTargetComponentLayoutOptions options;
    options.required_absolute_targets.push_back(
        core::NaturalTargetRequiredAbsoluteTarget{
            .target_item = helper_item,
            .target_address = 8,
        });
    const Observation before = observe(input.items, input.preloads);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    const auto final_flow = flow(Fixture{.items = rewritten.items,
                                        .preloads = rewritten.preloads});
    const int retained_indirect_calls = static_cast<int>(std::count_if(
        final_flow.indirect_flow_targets.begin(),
        final_flow.indirect_flow_targets.end(), [&](const auto& entry) {
          return entry.first < rewritten.items.size() &&
                 rewritten.items.at(entry.first).kind == MachineItemKind::Op &&
                 rewritten.items.at(entry.first).opcode == 0xa9 &&
                 entry.second.size() == 1U && entry.second.front().address == 8;
        }));
    std::string same_target_diagnostics;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!same_target_diagnostics.empty())
        same_target_diagnostics += " | ";
      same_target_diagnostics += reason;
    }
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 1 &&
                rewritten.plan.rebound_indirect_flows == 0 &&
                preload_map(rewritten.preloads).at("9") == "8" &&
                retained_indirect_calls == 4,
            "relocating a selector to the same typed callee should preserve its existing "
            "one-cell consumers while converting direct calls: proved=" +
                std::to_string(rewritten.plan.proved) + " applied=" +
                std::to_string(rewritten.applied) + " removed=" +
                std::to_string(rewritten.removed_cells) + " rebound=" +
                std::to_string(rewritten.plan.rebound_indirect_flows) +
                " preload=" + preload_map(rewritten.preloads).at("9") +
                " calls=" + std::to_string(retained_indirect_calls) +
                " reasons=" + same_target_diagnostics);
    std::map<std::string, std::string> before_observable = before.state;
    std::map<std::string, std::string> after_observable = after.state;
    before_observable.erase("9");
    after_observable.erase("9");
    require(before.stopped && after.stopped &&
                before_observable == after_observable,
            "same-target selector relocation must preserve emulator-visible state");
  }

  {
    const Fixture input = companion_address_selector_rebind_fixture();
    core::NaturalTargetComponentLayoutOptions options;
    options.maximum_anchors = 1;
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 3 && final_preloads.at("8") == "18" &&
                final_preloads.at("9") != "C7" &&
                std::any_of(rewritten.plan.preloads.begin(), rewritten.plan.preloads.end(),
                            [](const core::NaturalTargetPreloadRewrite& preload) {
                              return preload.register_name == "9" &&
                                     preload.old_value == "C7";
                            }),
            "natural-target layout should retarget an independent existing raw-BCD "
            "address-only selector after moving another helper");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    std::map<std::string, std::string> before_observable = before.state;
    std::map<std::string, std::string> after_observable = after.state;
    before_observable.erase("9");
    after_observable.erase("9");
    require(before.stopped && after.stopped &&
                before_observable == after_observable,
            "companion address-only selector rebinding must preserve machine state");

    const Fixture data_visible = companion_address_selector_rebind_fixture(true);
    const auto rejected = core::optimize_natural_target_component_layout(
        data_visible.items, data_visible.preloads, flow(data_visible), options);
    require(!rejected.plan.proved && rejected.applied == 0 &&
                preload_map(rejected.preloads).at("9") == "C7",
            "an independently observed selector must not be rebound merely to enable "
            "another natural target");
  }

  {
    const Fixture input = paid_alignment_fixture();
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 2 &&
                cell_count(rewritten.items) == cell_count(input.items) - 2 &&
                rewritten.items.at(item_at_address(rewritten.items, 19)).opcode == 0x54 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x22,
            "a profitable natural target may spend one unreachable alignment cell");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "paid natural-target alignment must preserve observable machine state");
  }

  {
    const Fixture input = multi_anchor_fixture();
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved, "multi-anchor synthetic fixture should have an exact CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    const int calls_through_8 = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.selector_register == "8" && flow.target_address == 20;
        }));
    const int calls_through_9 = static_cast<int>(std::count_if(
        rewritten.plan.flows.begin(), rewritten.plan.flows.end(), [](const auto& flow) {
          return flow.selector_register == "9" && flow.target_address == 30;
        }));
    require(rewritten.plan.proved && rewritten.applied == 5 &&
                rewritten.removed_cells == 5 && calls_through_8 == 2 &&
                calls_through_9 == 3 &&
                rewritten.items.at(item_at_address(rewritten.items, 20)).opcode == 0x22 &&
                rewritten.items.at(item_at_address(rewritten.items, 30)).opcode == 0x23,
            "independent constants should place and prove two unrelated helpers jointly");
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "joint natural-target layout must preserve the observable machine state");
  }

  {
    const Fixture input = fixture(2, 3, true);
    const auto direct_call = std::find_if(
        input.items.begin(), input.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x53;
        });
    require(direct_call != input.items.end(),
            "required-selector fixture should expose a direct call");
    const std::size_t call_item = static_cast<std::size_t>(
        std::distance(input.items.begin(), direct_call));
    const auto baseline = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const auto baseline_flow = std::find_if(
        baseline.plan.flows.begin(), baseline.plan.flows.end(),
        [&](const core::NaturalTargetFlowRewrite& rewrite) {
          return rewrite.original_command_item == call_item;
        });
    require(baseline.plan.proved && baseline_flow != baseline.plan.flows.end(),
            "required-selector fixture should have a proved baseline target identity");
    const std::size_t helper_item = baseline_flow->original_target_item;
    core::NaturalTargetComponentLayoutOptions required;
    required.required_flow_selectors.push_back(
        core::NaturalTargetRequiredFlowSelector{
            .command_item = call_item,
            .register_name = "8",
        });
    required.required_selector_targets.push_back(
        core::NaturalTargetRequiredSelectorTarget{
            .target_item = helper_item,
            .register_name = "8",
        });
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), required);
    require(rewritten.plan.proved &&
                std::any_of(rewritten.plan.flows.begin(), rewritten.plan.flows.end(),
                            [&](const core::NaturalTargetFlowRewrite& rewrite) {
                              return rewrite.original_command_item == call_item &&
                                     rewrite.selector_register == "8";
                            }) &&
                rewritten.items.at(item_at_address(
                    rewritten.items, input.selector_target)).opcode == 0x22,
            "typed required flow and zero-flow selector targets should share the exact "
            "proved command identity");

    required.required_selector_targets.front().target_item = input.items.size();
    const auto invalid = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), required);
    require(!invalid.plan.proved && invalid.applied == 0,
            "an invalid required selector target must fail closed");
  }

  for (const auto [calls, components] :
       {std::pair{2, 3}, std::pair{3, 5}, std::pair{5, 6}}) {
    const Fixture input = fixture(calls, components, true);
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved, "unrelated synthetic fixture should have an exact CFG");
    const core::NaturalTargetComponentLayoutResult rewritten =
        core::optimize_natural_target_component_layout(
            input.items, input.preloads, input_flow);
    require(rewritten.plan.proved && rewritten.removed_cells == calls &&
                rewritten.applied == calls &&
                rewritten.plan.selector_origin ==
                    core::NaturalTargetSelectorOrigin::ExistingPreload &&
                rewritten.plan.selector_register == "8" &&
                rewritten.plan.natural_target == input.selector_target &&
                rewritten.plan.flows.size() == static_cast<std::size_t>(calls) &&
                cell_count(rewritten.items) == cell_count(input.items) - calls,
            "arbitrary call count and component count should use the same exact planner: calls=" +
                std::to_string(calls) + " applied=" + std::to_string(rewritten.applied) +
                " removed=" + std::to_string(rewritten.removed_cells) +
                " reason=" + (rewritten.plan.reasons.empty()
                                    ? std::string("none")
                                    : rewritten.plan.reasons.front()));
    require(rewritten.plan.control_flow_equivalent &&
                rewritten.plan.call_return_equivalent &&
                rewritten.plan.stack_and_x2_equivalent &&
                rewritten.plan.indirect_memory_equivalent &&
                rewritten.plan.data_projection_equivalent &&
                rewritten.plan.final_control_flow.proved,
            "all independent proof obligations should be retained in the result");
    require(rewritten.plan.runtime_selectors.size() ==
                static_cast<std::size_t>(calls + 1) &&
                std::all_of(
                    rewritten.plan.runtime_selectors.begin(),
                    rewritten.plan.runtime_selectors.end(),
                    [](const core::NaturalTargetRuntimeSelectorProof& proof) {
                      return proof.stable_mutation_class && proof.selector_unwritten &&
                             proof.typed_target_matches_runtime_decode &&
                             proof.decoded_target == proof.final_target_address;
                    }) &&
                static_cast<int>(std::count_if(
                    rewritten.plan.runtime_selectors.begin(),
                    rewritten.plan.runtime_selectors.end(),
                    [&](const core::NaturalTargetRuntimeSelectorProof& proof) {
                      return proof.register_name == "8" &&
                             proof.delivered_preload ==
                                 std::to_string(input.selector_target) &&
                             proof.decoded_target == input.selector_target;
                    })) == calls,
            "every typed selector must independently decode from its delivered preload");
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(final_preloads.at("8") == std::to_string(input.selector_target),
            "stable natural selector must retain its exact ordinary data value");
    require(final_preloads.at("b") != input.old_fractional_selector &&
                final_preloads.at("b").ends_with(".375") &&
                std::any_of(rewritten.plan.preloads.begin(), rewritten.plan.preloads.end(),
                            [](const core::NaturalTargetPreloadRewrite& preload) {
                              return preload.register_name == "b" &&
                                     preload.fractional_projection_only;
                            }),
            "relocated indirect flow should rebind only the proved-dead integer projection");

    const std::size_t helper_item = item_at_address(rewritten.items, input.selector_target);
    require(rewritten.items.at(helper_item).opcode == 0x22,
            "callee identity should land exactly on the selector's natural address");
    require(static_cast<int>(std::count_if(
                rewritten.items.begin(), rewritten.items.end(), [](const MachineItem& item) {
                  return item.kind == MachineItemKind::Op && item.opcode == 0xa8;
                })) == calls,
            "every equivalent direct call should become one stable indirect call");
  }

  {
    const Fixture input = fixture(2, 4, true);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(before.stopped && after.stopped && before.state == after.state,
            "emulator must preserve X/Y/Z/T, hidden X2, and the data use of R8: before=" +
                before.state.at("x") + "/" + before.state.at("x1") + "/" +
                before.state.at("8") + " after=" + after.state.at("x") + "/" +
                after.state.at("x1") + "/" + after.state.at("8"));
  }

  {
    Fixture input = fixture(2, 5, true, 76);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.value = "0.41200076";
    }
    const auto selector_recall =
        std::find_if(input.items.begin(), input.items.end(), [](const MachineItem& item) {
          return item.kind == MachineItemKind::Op && item.opcode == 0x68;
        });
    require(selector_recall != input.items.end(),
            "fractional-selector fixture should contain an ordinary data recall");
    const std::size_t recall_index =
        static_cast<std::size_t>(std::distance(input.items.begin(), selector_recall));
    require(recall_index + 1U < input.items.size() &&
                input.items.at(recall_index + 1U).opcode == 0x35,
            "fractional-selector fixture should expose the expected projection use");
    input.items.at(recall_index + 1U) = op(0x12);

    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved,
            "fractional-selector fixture should have an exact original CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.plan.selector_register == "8" &&
                rewritten.plan.natural_target == 76 &&
                final_preloads.at("8") == "4.1200076E-1",
            "a hidden fractional constant should retain its numeric value while its canonical "
            "BCD entry supplies a proved natural call target: " +
                (rewritten.plan.reasons.empty() ? std::string("no reason")
                                                : rewritten.plan.reasons.front()));
    const Observation before = observe(input.items, input.preloads, true);
    const Observation after = observe(rewritten.items, rewritten.preloads, true);
    require(before.stopped && after.stopped && before.state == after.state,
            "fractional exponent selector must preserve its numeric data projection: before=" +
                before.state.at("x") + "/R8=" + before.state.at("8") + " after=" +
                after.state.at("x") + "/R8=" + after.state.at("8"));
  }

  {
    Fixture input;
    input.items.push_back(MachineItem::label("retunable_fraction_entry"));
    input.items.push_back(op(0x01));
    input.items.push_back(op(0x53));
    input.items.push_back(MachineItem::address("retunable_fraction_helper"));
    input.visible_stop = input.items.size();
    input.items.push_back(stop());
    input.items.push_back(MachineItem::label("retunable_fraction_helper"));
    MachineItem selector_recall = op(0x67);
    selector_recall.roles.push_back(
        std::string(kRetunableNaturalFractionalSelectorRolePrefix) + "0.226000");
    input.items.push_back(std::move(selector_recall));
    input.items.push_back(op(0x12));
    input.items.push_back(op(0x15));
    input.items.push_back(op(0x34));
    input.items.push_back(op(0x52));
    input.preloads.push_back(PreloadReport{
        .register_name = "7",
        .value = "0.226",
        .retunable_natural_fractional_prefix = "0.226000",
    });

    const Observation before = observe(input.items, input.preloads, true);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const Observation after = observe(rewritten.items, rewritten.preloads, true);
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 1 &&
                rewritten.removed_cells == 1 &&
                final_preloads.at("7") == "2.2600003E-1",
            "a proved natural-fractional family should receive the helper's final address");
    require(before.stopped && after.stopped &&
                before.state.at("x") == after.state.at("x"),
            "retuning the proved mantissa suffix must preserve its observable projection");
  }

  {
    Fixture input;
    const std::string old_helper = "retunable_rebind_old_helper";
    const std::string new_helper = "retunable_rebind_new_helper";
    input.items.push_back(MachineItem::label("retunable_rebind_entry"));
    MachineItem selector_recall = op(0x67);
    selector_recall.roles.push_back(
        std::string(kRetunableNaturalFractionalSelectorRolePrefix) + "0.226000");
    input.items.push_back(std::move(selector_recall));
    input.items.push_back(op(0x12));
    input.items.push_back(op(0x15));
    input.items.push_back(op(0x34));
    MachineItem old_indirect_call = op(0xa7);
    old_indirect_call.indirect_flow_targets = std::vector<IrTarget>{old_helper};
    input.items.push_back(std::move(old_indirect_call));
    for (int call = 0; call < 3; ++call) {
      input.items.push_back(op(0x53));
      input.items.push_back(MachineItem::address(new_helper));
    }
    input.visible_stop = input.items.size();
    input.items.push_back(stop());
    input.items.push_back(MachineItem::label(old_helper));
    input.items.push_back(op(0x0b));
    input.items.push_back(op(0x52));
    input.items.push_back(MachineItem::label(new_helper));
    input.items.push_back(op(0x22));
    input.items.push_back(op(0x52));
    input.preloads.push_back(PreloadReport{
        .register_name = "7",
        .value = "2.2600012E-1",
        .retunable_natural_fractional_prefix = "0.226000",
    });

    const Observation before = observe(input.items, input.preloads, true);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const Observation after = observe(rewritten.items, rewritten.preloads, true);
    const std::map<std::string, std::string> final_preloads =
        preload_map(rewritten.preloads);
    std::vector<int> final_selector_targets;
    for (const auto& [source_item, targets] :
         rewritten.plan.final_control_flow.indirect_flow_targets) {
      if (source_item >= rewritten.items.size() ||
          rewritten.items.at(source_item).kind != MachineItemKind::Op ||
          rewritten.items.at(source_item).opcode != 0xa7) {
        continue;
      }
      for (const core::PostLayoutCommandIdentity& target : targets)
        final_selector_targets.push_back(target.address);
    }
    std::sort(final_selector_targets.begin(), final_selector_targets.end());
    final_selector_targets.erase(
        std::unique(final_selector_targets.begin(), final_selector_targets.end()),
        final_selector_targets.end());
    const std::optional<core::IndirectAddressEvaluation> delivered_selector =
        core::evaluate_indirect_address(
            "7", final_preloads.at("7"), core::IndirectOperationKind::Flow);
    require(rewritten.plan.proved && rewritten.applied == 3 &&
                rewritten.removed_cells == 2 &&
                rewritten.plan.rebound_indirect_flows == 1 &&
                final_selector_targets.size() == 1U &&
                delivered_selector.has_value() &&
                delivered_selector->actual_flow_target ==
                    final_selector_targets.front(),
            "a retunable fractional selector should move from one old flow to three new calls: "
            "proved=" + std::to_string(rewritten.plan.proved) +
                ", applied=" + std::to_string(rewritten.applied) +
                ", removed=" + std::to_string(rewritten.removed_cells) +
                ", rebound=" + std::to_string(rewritten.plan.rebound_indirect_flows) +
                ", preload=" + final_preloads.at("7") +
                ", typedTargets=" +
                std::to_string(final_selector_targets.size()) +
                ", firstReason=" +
                (rewritten.plan.reasons.empty() ? std::string("-")
                                                : rewritten.plan.reasons.front()));
    require(before.stopped && after.stopped && before.state == after.state,
            "fractional selector reassignment must preserve data and flow observations");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.register_name = "6";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0 &&
                reason_contains(rejected.plan, "no stable selector"),
            "pre-increment and pre-decrement selector classes must fail closed");
  }

  {
    const Fixture input = fixture(2, 3, true);
    core::AuthoritativePostLayoutControlFlow forged = flow(input);
    forged.proved = false;
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, forged);
    require(!rejected.plan.proved &&
                reason_contains(rejected.plan, "not authoritative"),
            "an unproved input CFG must never authorize layout mutation");
  }

  {
    Fixture input = fixture(2, 3, true);
    const int helper_address = 2 * 2 + 7;
    for (MachineItem& item : input.items) {
      if (item.kind == MachineItemKind::Address) {
        item.formal_opcode = official_address_to_opcode(helper_address);
        break;
      }
    }
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved,
            "ordinary formal-address fixture should have a valid original CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.removed_cells == 2,
            "wrapped ordinary formal address operands should follow their proved label identity");
  }

  {
    Fixture input = fixture(2, 3, true);
    const auto helper = std::find_if(input.items.begin(), input.items.end(),
                                     [](const MachineItem& item) {
                                       return item.kind == MachineItemKind::Label &&
                                              item.name.starts_with("opaque_leaf_");
                                     });
    require(helper != input.items.end(), "overflow fixture should contain its helper label");
    const std::size_t helper_index =
        static_cast<std::size_t>(std::distance(input.items.begin(), helper));
    require(helper_index + 2U < input.items.size(),
            "overflow fixture helper should contain a command and return");
    std::vector<MachineItem> moved_helper(
        input.items.begin() + static_cast<std::ptrdiff_t>(helper_index),
        input.items.begin() + static_cast<std::ptrdiff_t>(helper_index + 3U));
    input.items.erase(input.items.begin() + static_cast<std::ptrdiff_t>(helper_index),
                      input.items.begin() + static_cast<std::ptrdiff_t>(helper_index + 3U));
    const std::vector<MachineItem> old_helper_replacement{
        MachineItem::label("old_helper_replacement"), op(0x0d), stop()};
    input.items.insert(input.items.begin() + static_cast<std::ptrdiff_t>(helper_index),
                       old_helper_replacement.begin(), old_helper_replacement.end());
    std::vector<MachineItem> overflow_padding;
    overflow_padding.push_back(MachineItem::label("overflow_padding"));
    for (int cell = 0; cell < 69; ++cell)
      overflow_padding.push_back(op(0x0d));
    overflow_padding.push_back(stop());
    input.items.insert(input.items.end(), overflow_padding.begin(), overflow_padding.end());
    input.items.insert(input.items.end(), moved_helper.begin(), moved_helper.end());
    const int moved_helper_address = cell_count(input.items) - 2;
    for (MachineItem& item : input.items) {
      if (item.kind == MachineItemKind::Address &&
          std::holds_alternative<std::string>(item.target) &&
          std::get<std::string>(item.target).starts_with("opaque_leaf_")) {
        item.target = moved_helper_address;
        item.formal_opcode = 0x85;
      }
    }
    const core::AuthoritativePostLayoutControlFlow input_flow = flow(input);
    require(input_flow.proved,
            "wrapped over-window fixture should still have a physical input CFG");
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, input_flow);
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.removed_cells == 2 && rewritten.plan.natural_target == 34,
            "over-window calls should follow normalized label identities into proved layout: " +
                (rewritten.plan.reasons.empty() ? std::string("no reason")
                                                : rewritten.plan.reasons.front()) +
                "; applied=" + std::to_string(rewritten.applied));
  }

  {
    Fixture input = fixture(2, 3, true);
    for (MachineItem& item : input.items) {
      if (item.kind == MachineItemKind::Address) {
        item.roles.push_back("opaque-extra-contract");
        break;
      }
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "an address operand with an opaque secondary contract must fail closed");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8") {
        preload.setup_expression = true;
        preload.setup_expression_text = "opaque_runtime_expression()";
        preload.setup_source_line = 71;
      }
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "a computed setup value must not be trusted as a literal call selector");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "b") {
        preload.setup_expression = true;
        preload.setup_expression_text = "another_opaque_expression()";
      }
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "a computed setup selector must not be rebound by replacing only report.value");
  }

  {
    Fixture input = fixture(2, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.setup_target_name = "opaque_generated_setup_target";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "a preload owned by generated setup must fail closed conservatively");
  }

  {
    Fixture input = fixture(1, 3, true);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "b")
        preload.value = "12.12345678";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "fractional projection must fail closed when relocating a selector changes the "
            "number of integer digits and therefore MK-61 mantissa precision");
  }

  {
    Fixture input = fixture(2, 5, true, 89);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.value = "123456789";
    }
    const auto rejected = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(!rejected.plan.proved && rejected.applied == 0,
            "runtime selector proof must reject decimal preload text whose MK-61 mantissa "
            "canonicalization changes the indirect target");
  }

  {
    Fixture input = fixture(2, 5, true, 99);
    for (PreloadReport& preload : input.preloads) {
      if (preload.register_name == "8")
        preload.value = "-99999999";
    }
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    require(rewritten.plan.proved && rewritten.applied == 2 &&
                rewritten.plan.selector_register == "8" &&
                rewritten.plan.natural_target == 99 &&
                std::all_of(rewritten.plan.runtime_selectors.begin(),
                            rewritten.plan.runtime_selectors.end(),
                            [](const core::NaturalTargetRuntimeSelectorProof& proof) {
                              return proof.register_name != "8" ||
                                     (proof.delivered_preload == "-99999999" &&
                                      proof.decoded_target == 99 &&
                                      proof.typed_target_matches_runtime_decode);
                            }),
            "an exact signed eight-digit preload should retain its sign while serving as a "
            "proved natural-target selector");
  }

  {
    const std::vector<MachineItem> uses = {
        MachineItem::op(0x67, "П->X 7"),
        MachineItem::op(0x12, "*"),
    };
    const PreloadReport retunable{
        .register_name = "7",
        .value = "2.2600021E-1",
        .retunable_natural_fractional_prefix = "0.226000",
    };
    const std::optional<std::string> rebound =
        core::rebind_proved_natural_fractional_selector_preload(
            uses, retunable, 21, 22);
    require(rebound == "2.2600022E-1",
            "a proved natural fractional selector family should retarget its final BCD digits");

    PreloadReport ordinary = retunable;
    ordinary.retunable_natural_fractional_prefix.reset();
    require(!core::rebind_proved_natural_fractional_selector_preload(
                 uses, ordinary, 21, 22)
                 .has_value(),
            "an untagged numeric preload must not be retargeted");

    MachineItem first_packed_recall = MachineItem::op(0x6c, "П->X c");
    first_packed_recall.roles.push_back(
        std::string(kRetunableNaturalFractionalSelectorRolePrefix) +
        "packed:888888");
    MachineItem second_packed_recall = first_packed_recall;
    const std::vector<MachineItem> packed_uses = {
        std::move(first_packed_recall), MachineItem::op(0x37, "К ∧"),
        std::move(second_packed_recall), MachineItem::op(0x37, "К ∧"),
    };
    const PreloadReport packed{
        .register_name = "c",
        .value = "88888834",
        .retunable_natural_fractional_prefix = "packed:888888",
    };
    require(core::rebind_proved_natural_fractional_selector_preload(
                packed_uses, packed, 34, 19) == "88888819",
            "a proved packed mask/address family should retarget every shared selector use");

    PreloadReport malformed_packed = packed;
    malformed_packed.retunable_natural_fractional_prefix = "packed:88888";
    require(!core::rebind_proved_natural_fractional_selector_preload(
                 packed_uses, malformed_packed, 34, 19)
                 .has_value(),
            "a malformed packed mask/address family must fail closed");
  }

  {
    const auto jump_fold_fixture = [](bool raw_jump) {
      Fixture input = fixture(2, 4, true);
      const std::string sink = "generic_fallthrough_jump_sink";
      MachineItem jump = op(0x51);
      jump.raw = raw_jump;
      input.items.at(input.visible_stop) = std::move(jump);
      input.items.insert(input.items.begin() +
                             static_cast<std::ptrdiff_t>(input.visible_stop + 1U),
                         MachineItem::address(sink));
      const std::size_t separator = input.old_fractional_selector.find('.');
      input.old_fractional_selector =
          std::to_string(std::stoi(input.old_fractional_selector.substr(0, separator)) + 1) +
          input.old_fractional_selector.substr(separator);
      for (PreloadReport& preload : input.preloads) {
        if (preload.register_name == "b")
          preload.value = input.old_fractional_selector;
      }
      input.items.push_back(MachineItem::label(sink));
      input.items.push_back(stop());
      return input;
    };

    const Fixture input = jump_fold_fixture(false);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input));
    const Observation before = observe(input.items, input.preloads);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    std::string fold_diagnostics;
    for (const std::string& reason : rewritten.plan.reasons) {
      if (!fold_diagnostics.empty())
        fold_diagnostics += " | ";
      fold_diagnostics += reason;
    }
    require(rewritten.plan.proved && rewritten.plan.fallthrough_jump_folds == 1 &&
                rewritten.plan.removed_cells >= 4 &&
                cell_count(input.items) - cell_count(rewritten.items) ==
                    rewritten.plan.removed_cells,
            "a terminal direct jump should chain its target component and account for both "
            "erased cells: proved=" + std::to_string(rewritten.plan.proved) +
                " folds=" +
                std::to_string(rewritten.plan.fallthrough_jump_folds) +
                " removed=" + std::to_string(rewritten.plan.removed_cells) +
                " reasons=" + fold_diagnostics);
    require(std::none_of(
                rewritten.plan.flows.begin(), rewritten.plan.flows.end(),
                [&](const core::NaturalTargetFlowRewrite& flow) {
                  return flow.original_command_item == input.visible_stop;
                }),
            "a two-cell fallthrough fold must outrank a competing one-cell "
            "selector rewrite of the same direct jump");
    require(before.stopped && after.stopped && before.state == after.state,
            "fallthrough jump component folding must preserve emulator-visible state");

    const Fixture raw = jump_fold_fixture(true);
    const auto blocked = core::optimize_natural_target_component_layout(
        raw.items, raw.preloads, flow(raw));
    require(blocked.plan.proved && blocked.plan.fallthrough_jump_folds == 0,
            "a raw terminal direct jump must remain an opaque layout barrier");
  }

  {
    const auto standalone_jump_fixture = [](bool raw_jump) {
      Fixture input;
      input.items.push_back(MachineItem::label("standalone_jump_entry"));
      input.items.push_back(op(0x54));
      MachineItem jump = op(0x51);
      jump.raw = raw_jump;
      input.items.push_back(std::move(jump));
      input.items.push_back(MachineItem::address("standalone_jump_sink"));
      input.items.push_back(MachineItem::label("standalone_jump_sink"));
      input.items.push_back(op(0x54));
      input.items.push_back(stop());
      return input;
    };

    core::NaturalTargetComponentLayoutOptions options;
    options.allow_standalone_fallthrough_jump_fold = true;
    const Fixture input = standalone_jump_fixture(false);
    const Observation before = observe(input.items, input.preloads);
    const auto rewritten = core::optimize_natural_target_component_layout(
        input.items, input.preloads, flow(input), options);
    const Observation after = observe(rewritten.items, rewritten.preloads);
    require(rewritten.plan.proved && rewritten.applied == 1 &&
                rewritten.plan.fallthrough_jump_folds == 1 &&
                rewritten.plan.removed_cells == 2 &&
                cell_count(rewritten.items) == cell_count(input.items) - 2,
            "standalone final-layout jump folding should not require a selector rewrite");
    require(before.stopped && after.stopped && before.state == after.state,
            "standalone fallthrough jump folding must preserve emulator-visible state");

    const Fixture raw = standalone_jump_fixture(true);
    const auto blocked = core::optimize_natural_target_component_layout(
        raw.items, raw.preloads, flow(raw), options);
    require(blocked.applied == 0 && blocked.plan.fallthrough_jump_folds == 0 &&
                cell_count(blocked.items) == cell_count(raw.items),
            "standalone jump folding must fail closed for a raw jump command");

    Fixture numeric_entry;
    numeric_entry.items.push_back(MachineItem::label("numeric_jump_entry"));
    numeric_entry.items.push_back(op(0x01));
    numeric_entry.items.push_back(op(0x51));
    numeric_entry.items.push_back(MachineItem::address("numeric_jump_sink"));
    numeric_entry.items.push_back(MachineItem::label("numeric_jump_sink"));
    numeric_entry.items.push_back(op(0x02));
    numeric_entry.items.push_back(stop());
    const auto numeric_blocked = core::optimize_natural_target_component_layout(
        numeric_entry.items, numeric_entry.preloads, flow(numeric_entry), options);
    require(numeric_blocked.applied == 0 &&
                numeric_blocked.plan.fallthrough_jump_folds == 0 &&
                cell_count(numeric_blocked.items) ==
                    cell_count(numeric_entry.items),
            "standalone jump folding must preserve an observable number-entry boundary");
  }

  {
    std::vector<MachineItem> items;
    items.push_back(MachineItem::label("erasure_entry"));
    MachineItem indirect = MachineItem::op(0xa7, "К ПП 7");
    indirect.indirect_flow_targets = std::vector<IrTarget>{"erasure_target"};
    items.push_back(std::move(indirect));
    items.push_back(MachineItem::op(0x00, "0"));
    const std::size_t erased_item = items.size();
    items.push_back(MachineItem::op(0x52, "В/О"));
    items.push_back(MachineItem::label("erasure_target"));
    const std::size_t target_item = items.size();
    items.push_back(MachineItem::op(0x22, "F x^2"));
    items.push_back(MachineItem::op(0x52, "В/О"));
    const std::vector<PreloadReport> preloads = {
        PreloadReport{.register_name = "7", .value = "3"},
    };
    core::AuthoritativePostLayoutControlFlow typed_flow;
    typed_flow.proved = true;
    typed_flow.indirect_flow_targets[1] = {
        core::PostLayoutCommandIdentity{
            .item_index = target_item,
            .address = 3,
            .labels = {"erasure_target"},
        },
    };
    const core::PreloadedIndirectFlowCellErasurePlan rebound =
        core::plan_preloaded_indirect_flow_cell_erasure(
            items, preloads, typed_flow, erased_item, 2);
    require(rebound.proved && rebound.preloads.front().value == "2" &&
                rebound.original_targets.at(1) == std::vector<int>{3} &&
                rebound.rebound_targets.at(1) == std::vector<int>{2},
            "an address-only stable selector should follow its exact command identity across a "
            "generic one-cell erasure");

    items.push_back(MachineItem::op(0x67, "П->X 7"));
    const core::PreloadedIndirectFlowCellErasurePlan unsafe =
        core::plan_preloaded_indirect_flow_cell_erasure(
            items, preloads, typed_flow, erased_item, 2);
    require(!unsafe.proved,
            "an untagged selector with a non-flow use must not be changed by cell erasure");

    std::vector<MachineItem> late_items;
    MachineItem late_consumer = MachineItem::op(0xae, "К ПП e");
    late_consumer.roles.push_back("late-decimal-selector-consumer");
    late_consumer.indirect_flow_targets =
        std::vector<IrTarget>{"late_target_1", "late_target_2"};
    late_items.push_back(std::move(late_consumer));
    const std::size_t late_erased = late_items.size();
    late_items.push_back(MachineItem::op(0x52, "В/О"));
    late_items.push_back(MachineItem::label("late_target_1"));
    const std::size_t late_target_1 = late_items.size();
    late_items.push_back(MachineItem::op(0x00, "0"));
    late_items.push_back(MachineItem::label("late_target_2"));
    const std::size_t late_target_2 = late_items.size();
    late_items.push_back(MachineItem::op(0x01, "1"));
    core::AuthoritativePostLayoutControlFlow late_flow;
    late_flow.proved = true;
    late_flow.indirect_flow_targets[0] = {
        core::PostLayoutCommandIdentity{
            .item_index = late_target_1,
            .address = 2,
            .labels = {"late_target_1"},
        },
        core::PostLayoutCommandIdentity{
            .item_index = late_target_2,
            .address = 3,
            .labels = {"late_target_2"},
        },
    };
    const core::PreloadedIndirectFlowCellErasurePlan late =
        core::plan_preloaded_indirect_flow_cell_erasure(
            late_items, {}, late_flow, late_erased, 1);
    require(late.proved && late.rebound_targets.at(0) == std::vector<int>({1, 2}),
            "late-bound multi-target selector charges should follow their typed identities at "
            "the final generic binding stage");
  }
}

} // namespace mkpro::tests

#ifdef MKPRO_STANDALONE_NATURAL_TARGET_COMPONENT_LAYOUT_TEST
int main() {
  try {
    mkpro::tests::natural_target_component_layout_is_generic_and_proof_gated();
    std::cout << "[PASS] natural_target_component_layout_is_generic_and_proof_gated\n";
  } catch (const std::exception& error) {
    std::cerr << "[FAIL] " << error.what() << '\n';
    return 1;
  }
  return 0;
}
#endif
