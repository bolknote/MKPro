#include "mkpro/core/passes/indirect_selector_integer_part.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp store(std::string register_name) {
  return make_store(std::move(register_name));
}

IrOp recall(std::string register_name) {
  return make_recall(std::move(register_name));
}

IrOp indirect_recall(std::string register_name, std::string comment = "") {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = std::move(register_name);
  op.opcode = 0xd0;
  op.meta.mnemonic = "К П->X";
  if (!comment.empty())
    op.meta.comment = std::move(comment);
  return op;
}

IrOp marked_fractional_indirect_recall(std::string register_name) {
  return indirect_recall(std::move(register_name),
                         "indexed recall cells; indirect-selector-integer-part=source");
}

core::passes::PassResult run_indirect_selector_integer_part(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::indirect_selector_integer_part(
      ops, core::passes::PassContext{.options = options});
}

} // namespace

void indirect_selector_integer_part_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {
        marked_fractional_indirect_recall("d"),
        plain(0x04, "4"),
        recall("d"),
        plain(0x34, "К [x]"),
        plain(0x11, "-"),
    };
    const core::passes::PassResult result = run_indirect_selector_integer_part(program);

    require(result.applied == 1,
            "indirect-selector-integer-part did not remove redundant integer-part op");
    require(result.optimizations.size() == 1,
            "indirect-selector-integer-part did not report optimization metadata");
    require(result.optimizations.at(0).name == "indirect-selector-integer-part-reuse",
            "indirect-selector-integer-part reported wrong optimization name");
    require(
        std::none_of(result.ops.begin(), result.ops.end(),
                     [](const IrOp& op) { return op.kind == IrKind::Plain && op.opcode == 0x34; }),
        "indirect-selector-integer-part left redundant К [x]");
  }

  {
    const std::vector<IrOp> program = {
        indirect_recall("d"), plain(0x04, "4"), recall("d"), plain(0x34, "К [x]"), plain(0x11, "-"),
    };
    const core::passes::PassResult result = run_indirect_selector_integer_part(program);

    require(result.applied == 0,
            "indirect-selector-integer-part accepted an unmarked selector proof");
    require(result.ops.size() == program.size(),
            "indirect-selector-integer-part changed unmarked selector program");
  }

  {
    const std::vector<IrOp> program = {
        marked_fractional_indirect_recall("d"),
        plain(0x00, "0"),
        store("d"),
        recall("d"),
        plain(0x34, "К [x]"),
    };
    const core::passes::PassResult result = run_indirect_selector_integer_part(program);

    require(result.applied == 0,
            "indirect-selector-integer-part kept proof after overwriting selector register");
    require(result.ops.size() == program.size(),
            "indirect-selector-integer-part changed overwritten selector program");
  }
}

} // namespace mkpro::tests
