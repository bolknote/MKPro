#pragma once

#include "mkpro/core/post_layout_indirect_flow.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

struct ReturnStackReturnStep {
  int stored_return_address = 0;
  int target_address = 0;
  std::vector<int> stack_after_return;
};

struct ReturnStackTailBlock {
  std::string label;
  std::vector<MachineItem> body;
};

struct ReturnStackStartupLayoutPlan {
  std::vector<MachineItem> items;
  int transitions = 0;
  int injected_call_sites = 0;
  int existing_call_sites = 0;
  int paid_call_sites = 0;
  int transition_savings = 0;
  int charge_cost = 0;
  int net_savings = 0;
  bool profitable = false;
  std::string strategy;
  std::string rejection_reason;
};

struct ReturnStackStartupLayoutOptions {
  int existing_call_sites = 0;
  int min_net_savings = 1;
};

struct DirtyReturnStackDispatchPlan {
  std::vector<int> clean_targets;
  std::vector<int> dirty_return_addresses;
  std::vector<int> dirty_targets;
  bool high_risk = true;
  bool enabled = false;
  std::string risk_reason;
  std::string rejection_reason;
};

struct DirtyReturnStackDispatchOptions {
  bool size_rescue = false;
  int min_dirty_targets = 1;
};

std::vector<int> mk61_return_stack_after_call(std::vector<int> stack,
                                              int stored_return_address);
std::optional<ReturnStackReturnStep> mk61_return_stack_after_return(
    const std::vector<int>& stack);
std::vector<ReturnStackReturnStep> simulate_mk61_return_stack(
    std::vector<int> stack, int return_count);
ReturnStackStartupLayoutPlan build_return_stack_startup_layout(
    const std::vector<ReturnStackTailBlock>& tails,
    const std::vector<MachineItem>& entry_body,
    const std::string& entry_label = "__return_stack_entry",
    const ReturnStackStartupLayoutOptions& options = {});
DirtyReturnStackDispatchPlan plan_dirty_return_stack_dispatch(std::vector<int> stack,
                                                              int return_count,
                                                              const DirtyReturnStackDispatchOptions&
                                                                  options = {});

PostLayoutIndirectFlowResult
optimize_post_layout_return_stack_script(const std::vector<MachineItem>& items);

} // namespace mkpro::core
