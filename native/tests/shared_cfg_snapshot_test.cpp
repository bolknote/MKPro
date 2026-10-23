#include "mkpro/core/post_layout_control_flow.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using mkpro::tests::require;

class CacheMode {
 public:
  explicit CacheMode(const char* mode) {
    if (const auto* old = std::getenv("MKPRO_NATIVE_CFG_CACHE")) old_ = old;
    set(mode);
  }
  ~CacheMode() { set(old_ ? old_->c_str() : nullptr); }
 private:
  static void set(const char* mode) {
#ifdef _WIN32
    (void)_putenv_s("MKPRO_NATIVE_CFG_CACHE", mode ? mode : "");
#else
    if (mode) (void)setenv("MKPRO_NATIVE_CFG_CACHE", mode, 1);
    else (void)unsetenv("MKPRO_NATIVE_CFG_CACHE");
#endif
  }
  std::optional<std::string> old_;
};

bool same_facts(const AuthoritativePostLayoutControlFlow& a,
                const AuthoritativePostLayoutControlFlow& b) {
  const auto fields = [](const auto& flow) {
    return std::tie(flow.proved, flow.address_space_model, flow.external_entries,
        flow.indirect_flow_targets, flow.indirect_memory_targets, flow.empty_return_target,
        flow.execution_states, flow.execution_successors, flow.execution_edges,
        flow.maximum_observed_return_depth, flow.explored_states, flow.reasons);
  };
  return fields(a) == fields(b);
}

std::vector<MachineItem> nested_calls(int depth) {
  std::vector<MachineItem> items{MachineItem::op(0x53, "call"),
      MachineItem::address(std::string("leaf0")), MachineItem::op(0x50, "stop")};
  items.back().stop_disposition = StopDisposition::Terminal;
  for (int level = 0; level < depth; ++level) {
    items.push_back(MachineItem::label("leaf" + std::to_string(level)));
    if (level + 1 < depth) {
      items.push_back(MachineItem::op(0x53, "call"));
      items.push_back(MachineItem::address("leaf" + std::to_string(level + 1)));
    } else {
      items.push_back(MachineItem::op(7, "value"));
    }
    items.push_back(MachineItem::op(0x52, "return"));
  }
  return items;
}

void snapshot_contracts(AddressSpaceModel model) {
  CacheMode enabled("1");
  auto items = nested_calls(5);
  PostLayoutControlFlowOptions options;
  options.address_space_model = model;
  const auto snapshot = shared_post_layout_control_flow(items, options);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(*snapshot)>>);
  require(snapshot->proved && snapshot->maximum_observed_return_depth == 5,
          "all five exact return frames are explored");
  require(shared_post_layout_control_flow(items, options) == snapshot,
          "an exact full-key hit shares the immutable graph without copying");
  const auto deepest = std::find_if(snapshot->execution_states.begin(), snapshot->execution_states.end(),
      [](const auto& state) { return state.address == 15 && state.return_stack.size() == 5; });
  require(deepest != snapshot->execution_states.end() &&
          deepest->return_stack == std::vector<int>({2, 5, 8, 11, 14}),
          "inline keys retain exact frame order and every continuation");
  {
    CacheMode disabled("0");
    const auto first = shared_post_layout_control_flow(items, options);
    const auto second = shared_post_layout_control_flow(items, options);
    require(first != second && same_facts(*first, *snapshot) && same_facts(*second, *snapshot),
            "uncached exploration produces exactly the same complete fact set");
  }
  auto mutable_copy = build_post_layout_control_flow(items, options);
  require(same_facts(mutable_copy, *snapshot), "the value API retains identical facts");
  mutable_copy.proved = false;
  mutable_copy.execution_states.clear();
  mutable_copy.execution_edges.clear();
  require(snapshot->proved && !snapshot->execution_states.empty() &&
          same_facts(*shared_post_layout_control_flow(items, options), *snapshot),
          "mutating a value copy cannot corrupt a shared proof input");

  auto changed = items;
  changed.front().source_line = 29;
  require(shared_post_layout_control_flow(changed, options) != snapshot,
          "even descriptive item metadata remains part of exact cache identity");
  auto other_model = options;
  other_model.address_space_model = model == AddressSpaceModel::Standard
      ? AddressSpaceModel::Mk61SMiniExpanded : AddressSpaceModel::Standard;
  const auto other = shared_post_layout_control_flow(items, other_model);
  require(other != snapshot && other->address_space_model == other_model.address_space_model,
          "stock and 105+7 memory models cannot share a certificate");
  auto shallow = options;
  shallow.maximum_return_depth = 4;
  require(!shared_post_layout_control_flow(items, shallow)->proved,
          "configured depth limits still fail closed");
  auto invalid = options;
  invalid.maximum_return_depth = 6;
  require(!shared_post_layout_control_flow(items, invalid)->proved &&
          !shared_post_layout_control_flow(nested_calls(6), options)->proved,
          "neither configuration nor a sixth runtime frame can overrun the hardware bound");
  auto exhausted = options;
  exhausted.maximum_execution_states = 1;
  require(!shared_post_layout_control_flow(items, exhausted)->proved,
          "cache identity cannot bypass an exhausted exploration cap");
  auto bad_entry = options;
  bad_entry.main_entry = 1;
  require(!shared_post_layout_control_flow(items, bad_entry)->proved,
          "entry options cannot reuse an unrelated successful proof");
  for (int index = 1; index <= 17; ++index) {
    auto variant = items;
    variant.front().source_line = index;
    require(shared_post_layout_control_flow(variant, options)->proved, "eviction fixture is valid");
  }
  const auto renewed = shared_post_layout_control_flow(items, options);
  require(renewed != snapshot && same_facts(*renewed, *snapshot),
          "an owning snapshot survives bounded cache eviction unchanged");
}
}

int main() {
  try {
    snapshot_contracts(AddressSpaceModel::Standard);
    snapshot_contracts(AddressSpaceModel::Mk61SMiniExpanded);
    std::cout << "shared CFG snapshots and exact return-stack contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
