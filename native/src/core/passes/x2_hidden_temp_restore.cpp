#include "mkpro/core/passes/x2_hidden_temp_restore.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kDotOpcode = 0x0a;

IrOp dot_restore_op(const std::string& register_name, const IrOp& source) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = kDotOpcode;
  op.meta.mnemonic = ".";
  std::string comment;
  if (source.meta.comment.has_value() && !source.meta.comment->empty())
    comment = *source.meta.comment;
  const std::string restore = "restore " + register_name + " from hidden X2 temp";
  if (!comment.empty())
    comment += "; ";
  comment += restore;
  op.meta.comment = comment;
  return op;
}

} // namespace

PassResult x2_hidden_temp_restore(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  const std::vector<X2HiddenTempReplacement> replacements = x2_hidden_temp_restore_replacements(ops);
  if (replacements.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<int, std::string> replacement_registers;
  for (const X2HiddenTempReplacement& replacement : replacements)
    replacement_registers.emplace(replacement.index, replacement.register_name);

  std::vector<IrOp> result;
  result.reserve(ops.size());
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const auto found = replacement_registers.find(static_cast<int>(index));
    if (found == replacement_registers.end())
      result.push_back(ops.at(index));
    else
      result.push_back(dot_restore_op(found->second, ops.at(index)));
  }

  const std::size_t applied = replacements.size();
  const std::string detail = "Replaced " + std::to_string(applied) + " recall" +
                             (applied == 1U ? std::string{} : std::string{"s"}) +
                             " with . after proving the value already lives in X2 and the recall "
                             "stack lift is unused.";
  return PassResult{
      .ops = std::move(result),
      .applied = static_cast<int>(applied),
      .optimizations = {AppliedOptimization{.name = "x2-hidden-temp-restore", .detail = detail}},
  };
}

IrPass x2_hidden_temp_restore_pass() {
  return IrPass{
      .name = "x2-hidden-temp-restore",
      .run = x2_hidden_temp_restore,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
