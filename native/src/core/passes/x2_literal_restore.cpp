#include "mkpro/core/passes/x2_literal_restore.hpp"

#include "mkpro/core/passes/helpers.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::core::passes {

namespace {

constexpr int kDotOpcode = 0x0a;

IrOp dot_restore_op(const std::string& value, const IrOp& source) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = kDotOpcode;
  op.meta.mnemonic = ".";
  std::string comment;
  if (source.meta.comment.has_value() && !source.meta.comment->empty())
    comment = *source.meta.comment;
  const std::string restore = "restore literal " + value + " from hidden X2 temp";
  if (!comment.empty())
    comment += "; ";
  comment += restore;
  op.meta.comment = comment;
  return op;
}

} // namespace

PassResult x2_literal_restore(const std::vector<IrOp>& ops, const PassContext& context) {
  (void)context;
  const std::vector<X2LiteralReplacement> replacements = x2_literal_restore_replacements(ops);
  if (replacements.empty())
    return PassResult{.ops = ops, .applied = 0, .optimizations = {}};

  std::map<int, const X2LiteralReplacement*> replacement_by_start;
  for (const X2LiteralReplacement& replacement : replacements)
    replacement_by_start.emplace(replacement.start, &replacement);

  std::vector<IrOp> result;
  result.reserve(ops.size());
  int removed = 0;
  for (int index = 0; index < static_cast<int>(ops.size()); ++index) {
    const auto found = replacement_by_start.find(index);
    if (found == replacement_by_start.end()) {
      result.push_back(ops.at(static_cast<std::size_t>(index)));
      continue;
    }
    const X2LiteralReplacement& replacement = *found->second;
    result.push_back(dot_restore_op(replacement.display_value, ops.at(static_cast<std::size_t>(index))));
    removed += replacement.end - index;
    index = replacement.end;
  }

  const std::string detail = "Replaced " + std::to_string(removed) + " repeated numeric literal cell" +
                             (removed == 1 ? std::string{} : std::string{"s"}) +
                             " with hidden X2 . restore(s).";
  return PassResult{
      .ops = std::move(result),
      .applied = removed,
      .optimizations = {AppliedOptimization{.name = "x2-literal-restore", .detail = detail}},
  };
}

IrPass x2_literal_restore_pass() {
  return IrPass{
      .name = "x2-literal-restore",
      .run = x2_literal_restore,
      .layout_safe = false,
  };
}

} // namespace mkpro::core::passes
