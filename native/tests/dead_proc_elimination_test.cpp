#include "mkpro/core/passes/dead_proc_elimination.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp proc_start(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  op.procedure_boundary = "start";
  op.procedure_name = op.name;
  return op;
}

IrOp proc_end(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::string("\0proc_end_", 10) + name;
  op.procedure_boundary = "end";
  op.procedure_name = std::move(name);
  op.hidden = true;
  return op;
}

IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

IrOp jump_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.opcode = 0x51;
  op.target = std::move(target);
  op.meta.mnemonic = "БП";
  return op;
}

IrOp call_to(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.opcode = 0x53;
  op.target = std::move(target);
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp known_target_indirect_call(std::string selector, int target) {
  IrOp op;
  op.kind = IrKind::IndirectCall;
  op.register_name = std::move(selector);
  op.opcode = 0xa7;
  op.meta.mnemonic = "К ПП 7";
  op.meta.comment = "preloaded R7=2 indirect-target=" + std::to_string(target) +
                    " indirect flow";
  return op;
}

IrOp halt() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "С/П";
  return op;
}

IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "В/О";
  return op;
}

core::passes::PassResult run_dead_proc_elimination(const std::vector<IrOp>& ops) {
  const CompileOptions options;
  return core::passes::dead_proc_elimination(ops,
                                             core::passes::PassContext{.options = options});
}

bool has_label(const std::vector<IrOp>& ops, const std::string& name) {
  return std::any_of(ops.begin(), ops.end(), [&](const IrOp& op) {
    return op.kind == IrKind::Label && op.name == name;
  });
}

} // namespace

void dead_proc_elimination_matches_typescript_contract() {
  {
    const core::passes::PassResult result =
        run_dead_proc_elimination({jump_to("live"), proc_start("dead"), plain(0x09, "9"),
                                   ret(), proc_end("dead"), proc_start("live"), halt(),
                                   proc_end("live")});

    const bool reported = std::any_of(
        result.optimizations.begin(), result.optimizations.end(),
        [](const core::passes::AppliedOptimization& item) {
          return item.name == "dead-proc-elimination";
        });
    require(reported, "dead-proc-elimination did not report optimization");
    require(!has_label(result.ops, "dead"), "dead-proc-elimination kept dead proc label");
    require(has_label(result.ops, "live"), "dead-proc-elimination removed live proc label");
  }

  {
    const core::passes::PassResult result =
        run_dead_proc_elimination({known_target_indirect_call("7", 2), halt(),
                                   proc_start("helper"), plain(0x09, "9"), ret(),
                                   proc_end("helper")});

    require(result.applied == 0,
            "dead-proc-elimination removed proc reached through proven indirect call");
    require(has_label(result.ops, "helper"),
            "dead-proc-elimination dropped proven indirect call target");
  }

  {
    const core::passes::PassResult result =
        run_dead_proc_elimination({call_to("wait"), halt(), proc_start("wait"),
                                   proc_end("wait"), proc_start("resolve"),
                                   plain(0x09, "9"), ret(), proc_end("resolve")});

    require(result.applied == 0,
            "dead-proc-elimination removed proc reached by procedure fallthrough");
    require(has_label(result.ops, "resolve"),
            "dead-proc-elimination dropped procedure fallthrough tail");
  }
}

} // namespace mkpro::tests
