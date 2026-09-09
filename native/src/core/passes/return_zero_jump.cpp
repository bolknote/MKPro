#include "mkpro/core/passes/return_zero_jump.hpp"

#include "mkpro/core/post_layout_indirect_flow.hpp"

#include <string>
#include <vector>

namespace mkpro::core::passes {

PassResult return_zero_jump(const std::vector<IrOp>& ops, const PassContext& context) {
  const auto result = mkpro::core::optimize_post_layout_empty_stack_loop_return(
      lower_ir_to_machine(ops), context.options);
  if (result.applied == 0)
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};
  return PassResult{
      .ops = raise_machine_to_ir(result.items,
                                 effective_optimizer_feature_profile(context.options)),
      .applied = result.applied,
      .optimizations = {{
          .name = "return-zero-jump",
          .detail = "Replaced " + std::to_string(result.applied) +
                    " BP 01 sequence(s) with V/O after proving empty frames and unchanged "
                    "numeric, formal and symbolic destination identities.",
      }},
  };
}

IrPass return_zero_jump_pass() {
  return IrPass{
      .name = "return-zero-jump",
      .run = return_zero_jump,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
