#pragma once

#include "mkpro/core/ir.hpp"

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mkpro::core::passes {

struct OutlineOccurrence {
  std::string key;
  int start = 0;
  int end = 0;
  int cells = 0;
};

struct OutlineCandidate {
  std::string key;
  std::vector<OutlineOccurrence> occurrences;
  int cells = 0;
};

class LabelAllocator {
public:
  LabelAllocator(const std::vector<IrOp>& ops, std::string prefix);

  std::string next();

private:
  std::set<std::string> existing_;
  std::string prefix_;
  int counter_ = 0;
};

struct SelectedOutline {
  std::string label;
  OutlineOccurrence target;
  std::vector<OutlineOccurrence> replacements;
};

struct SuffixCollectionConfig {
  std::function<bool(const IrOp&)> is_terminal;
  std::function<bool(const IrOp&)> is_body_op;
  std::function<std::string(const IrOp&)> op_key;
};

std::string target_key(const IrTarget& target);
bool has_numeric_outline_flow_target(const std::vector<IrOp>& ops);
bool range_intersects(const std::set<int>& indexes, int start, int end);
void mark_range(std::set<int>& indexes, int start, int end);
std::vector<OutlineCandidate> collect_suffix_candidates(const std::vector<IrOp>& ops,
                                                        const SuffixCollectionConfig& config);
std::vector<SelectedOutline> select_shared_suffixes(
    const std::vector<OutlineCandidate>& candidates, LabelAllocator& labels,
    std::set<int>& protected_indexes);

} // namespace mkpro::core::passes
