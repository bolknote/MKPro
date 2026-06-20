#include "mkpro/core/passes/cse_display_block.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp recall(std::string register_name) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.register_name = std::move(register_name);
  op.opcode = 0x60;
  op.meta.mnemonic = "П->X";
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  return op;
}

core::passes::PassResult run_cse_display_block(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::cse_display_block(ops,
                                         core::passes::PassContext{.options = options});
}

bool has_cse_label(const std::vector<IrOp>& ops) {
  return std::any_of(ops.begin(), ops.end(), [](const IrOp& op) {
    return op.kind == IrKind::Label && op.name.rfind("__cse_block_", 0) == 0;
  });
}

std::string first_cse_label(const std::vector<IrOp>& ops) {
  for (const IrOp& op : ops) {
    if (op.kind == IrKind::Label && op.name.rfind("__cse_block_", 0) == 0)
      return op.name;
  }
  return "";
}

} // namespace

void cse_display_block_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {recall("1"), plain(0x01, "1"), plain(0x02, "2"),
                                       ret(), recall("1"), plain(0x01, "1"),
                                       plain(0x02, "2"), ret()};
    const core::passes::PassResult result = run_cse_display_block(program);

    require(result.applied == 1, "cse-display-block did not deduplicate matching block");
    require(has_cse_label(result.ops), "cse-display-block did not insert helper label");
    const std::string label_name = first_cse_label(result.ops);
    const bool has_jump = std::any_of(result.ops.begin(), result.ops.end(), [&](const IrOp& op) {
      return op.kind == IrKind::Jump && std::get<std::string>(op.target) == label_name &&
             op.meta.comment == "cse" && op.target_meta.comment == "cse jump";
    });
    require(has_jump, "cse-display-block did not replace duplicate with cse jump");
    require(result.optimizations.size() == 1,
            "cse-display-block did not report optimization");
    require(result.optimizations.at(0).name == "cse-display-block",
            "cse-display-block reported wrong optimization name");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x01, "1"), ret(),
                                       recall("1"), plain(0x01, "1"), ret()};
    const core::passes::PassResult result = run_cse_display_block(program);

    require(result.applied == 0, "cse-display-block deduplicated too-short blocks");
    require(result.ops.size() == program.size(),
            "cse-display-block changed too-short block program");
  }

  {
    const std::vector<IrOp> program = {recall("1"), plain(0x01, "1"), label("reset"),
                                       plain(0x02, "2"), ret(), recall("1"),
                                       plain(0x01, "1"), plain(0x02, "2"), ret()};
    const core::passes::PassResult result = run_cse_display_block(program);

    require(result.applied == 0, "cse-display-block crossed a label while collecting block");
    require(result.ops.size() == program.size(), "cse-display-block changed label-reset program");
  }
}

} // namespace mkpro::tests
