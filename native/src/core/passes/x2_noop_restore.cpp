#include "mkpro/core/passes/x2_noop_restore.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

PassResult x2_noop_restore(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  const std::vector<int> removed = x2_noop_restore_removed_indexes(ops);
  if (removed.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  const std::set<int> removed_set(removed.begin(), removed.end());
  std::vector<IrOp> filtered;
  filtered.reserve(ops.size() - removed_set.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (removed_set.count(static_cast<int>(index)) == 0)
      filtered.push_back(ops.at(index));
  }

  const std::size_t count = removed.size();
  const std::string detail = "Removed " + std::to_string(count) + " . restore" +
                             (count == 1U ? std::string{} : std::string{"s"}) +
                             " whose X2 value was already in X.";
  return PassResult{
      .ops = std::move(filtered),
      .applied = static_cast<int>(count),
      .optimizations = {AppliedOptimization{.name = "x2-noop-restore", .detail = detail}},
  };
}

IrPass x2_noop_restore_pass() {
  return IrPass{
      .name = "x2-noop-restore",
      .run = x2_noop_restore,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
