#include "mkpro/core/passes/x2_literal_restore.hpp"

#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

PassResult x2_literal_restore(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
}

IrPass x2_literal_restore_pass() {
  return IrPass{
      .name = "x2-literal-restore",
      .run = x2_literal_restore,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
