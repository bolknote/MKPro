#pragma once

#include "mkpro/core/formal_address.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/shared_helper_continuation.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mkpro::core {

struct SharedHelperWrapperOptions {
  AddressSpaceModel address_space_model = AddressSpaceModel::Standard;
  std::optional<int> empty_return_target = 1;
  std::size_t maximum_execution_states = 20000;
  std::map<std::size_t, std::vector<int>> proved_indirect_flow_targets;
};

struct SharedHelperWrapperProof {
  bool proved = false;
  bool continuation_proved = false;
  bool terminal_wrapper_proved = false;
  bool input_control_flow_proved = false;
  bool final_control_flow_proved = false;
  int input_cells = 0;
  int output_cells = 0;
  int removed_cells = 0;
  std::string helper_label;
  std::string wrapper_label;
  std::size_t redirected_call_item_index = 0;
  std::size_t terminal_call_item_index = 0;
  SharedHelperContinuationProof continuation;
  AuthoritativePostLayoutControlFlow final_control_flow;
  std::vector<std::optional<std::size_t>> old_to_new_item_indices;
  std::vector<std::string> reasons;
};

struct SharedHelperWrapperResult {
  std::vector<MachineItem> items;
  SharedHelperWrapperProof proof;
  std::vector<passes::AppliedOptimization> optimizations;
  int applied = 0;
  int removed_cells = 0;
};

// For a straight-line helper called exactly three times, prove that two calls
// have the same commutative join/direct-store continuation and the third does
// not. If one equal call is already terminal, label its existing
// `call; join; store; return` sequence as a wrapper, redirect the other equal
// call to that wrapper, and remove the now-duplicated join/store pair.
//
// The helper and divergent call are unchanged. The only new dynamic effect is
// one nested return-stack frame on the redirected path. The rewrite therefore
// requires both the complete stack/X2 continuation certificate and a fresh
// authoritative control-flow proof of the final artifact. Fixed/formal direct
// operands and unproved indirect flow fail closed.
SharedHelperWrapperResult optimize_shared_helper_wrapper(
    const std::vector<MachineItem>& items,
    const SharedHelperWrapperOptions& options = {});

} // namespace mkpro::core
