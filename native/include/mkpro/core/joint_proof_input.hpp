#pragma once

#include "mkpro/core/layout_search_artifact.hpp"
#include "mkpro/core/joint_constraint_origins.hpp"
#include "mkpro/core/natural_target_component_layout.hpp"

#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace mkpro::core::detail {
struct JointProofPreparationCounts {
  std::size_t inputs = 0, selector_proofs = 0, selector_reuses = 0;
};

// One immutable source of proof obligations, shared by the alternative
// ordinary/projected placements of that same source. Own snapshots rather
// than addresses into a mutable candidate or a hash of incomplete metadata.
class PreparedJointProofInput {
 public:
  PreparedJointProofInput(const std::vector<MachineItem>& items,
                         const std::vector<PreloadReport>& preloads,
                         PostLayoutControlFlowOptions options,
                         JointProofPreparationCounts* counts = nullptr,
                         std::size_t capacity = 64)
      : items_(items), preloads_(preloads), options_(std::move(options)),
        flow_(build_post_layout_control_flow(items_,options_)),
        counts_(counts), capacity_(capacity) {
    if (counts_) ++counts_->inputs;
  }
  const std::vector<MachineItem>& items() const { return items_; }
  const std::vector<PreloadReport>& preloads() const { return preloads_; }
  const PostLayoutControlFlowOptions& options() const { return options_; }
  const AuthoritativePostLayoutControlFlow& flow() const { return flow_; }
  const std::optional<JointOriginMap>& protected_runtime_charge_targets() {
    if (!runtime_charge_targets_prepared_) {
      runtime_charge_targets_=protected_joint_runtime_charge_targets(items_,&flow_,&preloads_);
      runtime_charge_targets_prepared_=true;
    }
    return runtime_charge_targets_;
  }
  std::size_t selector_proofs() const { return selector_proofs_; }
  std::size_t selector_reuses() const { return selector_reuses_; }
  std::size_t retained() const { return entries_.size(); }

  std::optional<std::string> rebind_selector(const PreloadReport& selected,
                                            int old_target, int new_target) {
    for (const auto& entry : entries_) {
      if (entry.old_target == old_target && entry.new_target == new_target &&
          same_word(entry.selected,selected)) {
        ++selector_reuses_;
        if (counts_) ++counts_->selector_reuses;
        return entry.result;
      }
    }
    ++selector_proofs_;
    if (counts_) ++counts_->selector_proofs;
    auto result = rebind_stable_preloaded_indirect_flow_selector(
        items_,selected,flow_,old_target,new_target,options_.address_space_model,&preloads_);
    // Store only completed outcomes; exceptions never create a certificate.
    // Every input other than the selected word/targets is frozen in this owner.
    if (entries_.size() < capacity_)
      entries_.push_back({selected,old_target,new_target,result});
    return result;
  }

 private:
  static bool same_word(const PreloadReport& a, const PreloadReport& b) {
    const auto fields = [](const PreloadReport& p) {
      return std::tie(p.register_name,p.value,p.counts_against_program,
          p.retunable_natural_fractional_prefix,p.setup_target_name,p.setup_expression,
          p.setup_expression_text,p.setup_source_line,p.lowered_data_value);
    };
    return fields(a) == fields(b);
  }
  struct Entry {
    PreloadReport selected;
    int old_target = -1, new_target = -1;
    std::optional<std::string> result;
  };
  const std::vector<MachineItem> items_;
  const std::vector<PreloadReport> preloads_;
  const PostLayoutControlFlowOptions options_;
  const AuthoritativePostLayoutControlFlow flow_;
  JointProofPreparationCounts* counts_;
  const std::size_t capacity_;
  std::vector<Entry> entries_;
  bool runtime_charge_targets_prepared_=false;
  std::optional<JointOriginMap> runtime_charge_targets_;
  std::size_t selector_proofs_ = 0, selector_reuses_ = 0;
};
} // namespace mkpro::core::detail
