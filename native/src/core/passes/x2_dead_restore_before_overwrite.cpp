#include "mkpro/core/passes/x2_dead_restore_before_overwrite.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

PassResult x2_dead_restore_before_overwrite(const std::vector<IrOp>& ops,
                                           const PassContext& context) {
  (void)context;
  return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
}

IrPass x2_dead_restore_before_overwrite_pass() {
  return IrPass{
      .name = "x2-dead-restore-before-overwrite",
      .run = x2_dead_restore_before_overwrite,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
