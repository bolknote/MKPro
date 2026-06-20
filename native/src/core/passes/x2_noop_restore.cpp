#include "mkpro/core/passes/x2_noop_restore.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

PassResult x2_noop_restore(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
}

IrPass x2_noop_restore_pass() {
  return IrPass{
      .name = "x2-noop-restore",
      .run = x2_noop_restore,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
