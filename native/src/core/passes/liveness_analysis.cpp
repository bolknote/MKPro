#include "mkpro/core/passes/liveness_analysis.hpp"

#include "mkpro/core/passes/cfg.hpp"

#include <set>
#include <string>
#include <vector>

namespace mkpro::core::passes {

namespace {

struct DefUse {
  RegisterValueSet defs;
  RegisterValueSet uses;
};

DefUse defs_and_uses(const IrOp& op) {
  switch (op.kind) {
  case IrKind::Store:
    return DefUse{.defs = RegisterValueSet{op.register_name}, .uses = {}};
  case IrKind::Recall:
    return DefUse{.defs = {}, .uses = RegisterValueSet{op.register_name}};
  case IrKind::IndirectRecall: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    RegisterValueSet uses{op.register_name};
    if (target.has_value())
      uses.insert(*target);
    return DefUse{.defs = {}, .uses = std::move(uses)};
  }
  case IrKind::IndirectStore: {
    const std::optional<std::string> target = known_indirect_memory_target(op);
    return DefUse{
        .defs = target.has_value() ? RegisterValueSet{*target} : RegisterValueSet{},
        .uses = RegisterValueSet{op.register_name},
    };
  }
  case IrKind::IndirectJump:
  case IrKind::IndirectCall:
  case IrKind::IndirectCondJump:
    return DefUse{.defs = {}, .uses = RegisterValueSet{op.register_name}};
  case IrKind::Loop: {
    const std::string counter = op.counter == "L0"   ? "0"
                                : op.counter == "L1" ? "1"
                                : op.counter == "L2" ? "2"
                                : op.counter == "L3" ? "3"
                                                     : "";
    return DefUse{.defs = RegisterValueSet{counter}, .uses = RegisterValueSet{counter}};
  }
  case IrKind::Label:
  case IrKind::Jump:
  case IrKind::CondJump:
  case IrKind::Call:
  case IrKind::Return:
  case IrKind::Stop:
  case IrKind::Plain:
  case IrKind::OrphanAddress:
    return DefUse{};
  }
  return DefUse{};
}

bool sets_equal(const RegisterValueSet& left, const RegisterValueSet& right) {
  return left == right;
}

} // namespace

LivenessInfo compute_liveness(const std::vector<IrOp>& ops, LivenessOptions options) {
  const std::vector<std::vector<int>> successors =
      build_cfg_successors(ops, BuildCfgOptions{
                                    .indirect_call_fallthrough = true,
                                    .unknown_indirect_flow_to_all =
                                        options.unknown_indirect_flow_to_all,
                                });
  const std::size_t size = ops.size();
  std::vector<RegisterValueSet> live_in(size);
  std::vector<RegisterValueSet> live_out(size);

  bool changed = true;
  int iterations = 0;
  while (changed && iterations < 200) {
    changed = false;
    ++iterations;

    for (int index = static_cast<int>(size) - 1; index >= 0; --index) {
      const std::size_t offset = static_cast<std::size_t>(index);
      RegisterValueSet new_out;
      for (const int successor : successors.at(offset)) {
        const RegisterValueSet& successor_in = live_in.at(static_cast<std::size_t>(successor));
        new_out.insert(successor_in.begin(), successor_in.end());
      }

      const DefUse def_use = defs_and_uses(ops.at(offset));
      RegisterValueSet new_in = def_use.uses;
      for (const std::string& reg : new_out) {
        if (!def_use.defs.contains(reg))
          new_in.insert(reg);
      }

      if (!sets_equal(new_in, live_in.at(offset)) || !sets_equal(new_out, live_out.at(offset))) {
        live_in.at(offset) = std::move(new_in);
        live_out.at(offset) = std::move(new_out);
        changed = true;
      }
    }
  }

  return LivenessInfo{.live_in = std::move(live_in), .live_out = std::move(live_out)};
}

} // namespace mkpro::core::passes
