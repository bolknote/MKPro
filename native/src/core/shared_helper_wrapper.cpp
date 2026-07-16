#include "mkpro/core/shared_helper_wrapper.hpp"

#include "mkpro/core/opcodes.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace mkpro::core {

namespace {

int cell_count(const std::vector<MachineItem>& items) {
  return static_cast<int>(std::count_if(items.begin(), items.end(),
                                        [](const MachineItem& item) {
                                          return item.kind != MachineItemKind::Label;
                                        }));
}

std::optional<std::size_t> next_cell_item(const std::vector<MachineItem>& items,
                                          std::size_t after) {
  for (++after; after < items.size(); ++after) {
    if (items.at(after).kind != MachineItemKind::Label)
      return after;
  }
  return std::nullopt;
}

IrKind basic_kind_for_opcode(int opcode) {
  const std::vector<IrOp> raised =
      raise_machine_to_ir({MachineItem::op(opcode, opcode_by_code(opcode).name)});
  return raised.empty() ? IrKind::Plain : raised.front().kind;
}

std::string unique_wrapper_label(const std::vector<MachineItem>& items) {
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }
  for (int suffix = 0;; ++suffix) {
    const std::string candidate =
        "__shared_continuation_wrapper_" + std::to_string(suffix);
    if (!labels.contains(candidate))
      return candidate;
  }
}

void append_comment(MachineItem& item, const std::string& suffix) {
  item.comment = item.comment.has_value() && !item.comment->empty()
                     ? std::optional<std::string>(*item.comment + "; " + suffix)
                     : std::optional<std::string>(suffix);
}

SharedHelperWrapperResult try_terminal_wrapper(
    const std::vector<MachineItem>& items,
    const SharedHelperContinuationProof& continuation,
    const SharedHelperContinuationCall& terminal,
    const SharedHelperWrapperOptions& options) {
  SharedHelperWrapperResult result;
  result.items = items;
  result.proof.continuation = continuation;
  result.proof.continuation_proved = continuation.proved;
  result.proof.input_cells = continuation.input_cells;
  result.proof.helper_label = continuation.helper_label;
  result.proof.terminal_call_item_index = terminal.call_item_index;

  const SharedHelperContinuationCall* redirected = nullptr;
  for (const SharedHelperContinuationCall& call : continuation.calls) {
    if (!call.ordinary || call.call_item_index == terminal.call_item_index)
      continue;
    if (redirected != nullptr) {
      result.proof.reasons.push_back(
          "terminal wrapper has more than one redirected ordinary call");
      return result;
    }
    redirected = &call;
  }
  if (redirected == nullptr) {
    result.proof.reasons.push_back(
        "terminal wrapper has no second ordinary call to redirect");
    return result;
  }
  result.proof.redirected_call_item_index = redirected->call_item_index;

  const std::optional<std::size_t> tail =
      next_cell_item(items, terminal.store_item_index);
  if (!tail.has_value() || items.at(*tail).kind != MachineItemKind::Op ||
      basic_kind_for_opcode(items.at(*tail).opcode) != IrKind::Return) {
    result.proof.reasons.push_back(
        "ordinary call selected as wrapper is not followed by a bare return");
    return result;
  }
  result.proof.terminal_wrapper_proved = true;

  PostLayoutControlFlowOptions flow_options;
  flow_options.address_space_model = options.address_space_model;
  flow_options.maximum_execution_states = options.maximum_execution_states;
  flow_options.main_entry = 0;
  flow_options.empty_return_target = options.empty_return_target;
  const AuthoritativePostLayoutControlFlow input_flow =
      build_post_layout_control_flow(items, flow_options);
  if (!input_flow.proved) {
    result.proof.reasons.push_back(
        "input control flow is not authoritative" +
        (input_flow.reasons.empty() ? std::string{}
                                    : ": " + input_flow.reasons.front()));
    return result;
  }
  result.proof.input_control_flow_proved = true;

  result.proof.wrapper_label = unique_wrapper_label(items);
  std::vector<MachineItem> rewritten;
  rewritten.reserve(items.size() + 1U);
  std::vector<std::optional<std::size_t>> old_to_new_item(items.size());
  for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
    if (item_index == terminal.call_item_index)
      rewritten.push_back(MachineItem::label(result.proof.wrapper_label));
    if (item_index == redirected->join_item_index ||
        item_index == redirected->store_item_index) {
      continue;
    }
    old_to_new_item.at(item_index) = rewritten.size();
    MachineItem item = items.at(item_index);
    if (item_index == redirected->operand_item_index) {
      item.target = result.proof.wrapper_label;
      item.formal_opcode.reset();
      append_comment(item, "shared terminal continuation wrapper");
    }
    rewritten.push_back(std::move(item));
  }
  result.proof.old_to_new_item_indices = std::move(old_to_new_item);

  result.proof.output_cells = cell_count(rewritten);
  result.proof.removed_cells =
      result.proof.input_cells - result.proof.output_cells;
  if (result.proof.removed_cells != continuation.continuation_cells ||
      result.proof.removed_cells <= 0) {
    result.proof.reasons.push_back(
        "wrapper rewrite did not remove exactly one duplicated continuation");
    return result;
  }

  const AuthoritativePostLayoutControlFlow final_flow =
      build_post_layout_control_flow(rewritten, flow_options);
  if (!final_flow.proved) {
    result.proof.reasons.push_back(
        "final control flow rejected the additional wrapper frame" +
        (final_flow.reasons.empty() ? std::string{}
                                    : ": " + final_flow.reasons.front()));
    return result;
  }
  result.proof.final_control_flow = final_flow;
  result.proof.final_control_flow_proved = true;
  result.proof.proved = true;
  result.items = std::move(rewritten);
  result.applied = 1;
  result.removed_cells = result.proof.removed_cells;
  result.optimizations.push_back(passes::AppliedOptimization{
      .name = "shared-helper-terminal-wrapper",
      .detail = "Redirected one of two byte-identical continuations of straight-line helper " +
                continuation.helper_label +
                " through the other call's existing terminal wrapper after complete "
                "stack/X2 and return-stack proofs; removed " +
                std::to_string(result.removed_cells) + " cells.",
  });
  return result;
}

} // namespace

SharedHelperWrapperResult optimize_shared_helper_wrapper(
    const std::vector<MachineItem>& items,
    const SharedHelperWrapperOptions& options) {
  SharedHelperWrapperResult first_rejection;
  first_rejection.items = items;
  bool best_near_match_exact_shape = false;
  std::size_t best_near_match_calls = 0;
  std::size_t best_near_match_ordinary_calls = 0;
  std::set<std::string> labels;
  for (const MachineItem& item : items) {
    if (item.kind == MachineItemKind::Label)
      labels.insert(item.name);
  }

  const SharedHelperContinuationOptions continuation_options{
      .address_space_model = options.address_space_model,
      .proved_indirect_flow_targets = options.proved_indirect_flow_targets,
  };
  for (const std::string& label : labels) {
    const SharedHelperContinuationProof continuation =
        verify_shared_helper_continuation(items, label, continuation_options);
    if (!continuation.proved) {
      const bool exact_shape =
          continuation.calls.size() == 3U &&
          continuation.ordinary_call_item_indices.size() == 2U;
      if (continuation.calls.size() >= 2U &&
          std::tuple{exact_shape,
                     continuation.ordinary_call_item_indices.size(),
                     continuation.calls.size()} >
              std::tuple{best_near_match_exact_shape,
                         best_near_match_ordinary_calls,
                         best_near_match_calls}) {
        best_near_match_exact_shape = exact_shape;
        best_near_match_calls = continuation.calls.size();
        best_near_match_ordinary_calls =
            continuation.ordinary_call_item_indices.size();
        first_rejection.proof.continuation = continuation;
        first_rejection.proof.helper_label = continuation.helper_label;
        first_rejection.proof.input_cells = continuation.input_cells;
        first_rejection.proof.reasons = continuation.reasons;
      }
      continue;
    }
    for (const SharedHelperContinuationCall& call : continuation.calls) {
      if (!call.ordinary)
        continue;
      SharedHelperWrapperResult candidate =
          try_terminal_wrapper(items, continuation, call, options);
      if (candidate.applied > 0)
        return candidate;
      if (!candidate.proof.reasons.empty() &&
          (first_rejection.proof.reasons.empty() ||
           (!first_rejection.proof.terminal_wrapper_proved &&
            candidate.proof.terminal_wrapper_proved))) {
        first_rejection.proof = std::move(candidate.proof);
      }
    }
  }
  return first_rejection;
}

} // namespace mkpro::core
