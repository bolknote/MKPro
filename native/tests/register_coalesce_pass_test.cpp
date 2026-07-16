#include "mkpro/core/passes/register_coalesce.hpp"
#include "mkpro/core/opcodes.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mkpro::tests {

namespace {

IrOp store(std::string register_name) {
  return make_store(std::move(register_name));
}

IrOp recall(std::string register_name) {
  return make_recall(std::move(register_name));
}

IrOp indirect_store(std::string selector, std::optional<std::string> targets = std::nullopt) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = std::move(selector);
  op.opcode = 0xb0 + register_index(op.register_name);
  op.meta.mnemonic = "К X->П " + op.register_name;
  if (targets.has_value())
    op.meta.comment = "indirect-memory-targets=" + *targets;
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

core::passes::PassResult run_register_coalesce(const std::vector<IrOp>& ops,
                                               CompileOptions options = CompileOptions{}) {
  return core::passes::register_coalesce(ops, core::passes::PassContext{.options = options});
}

} // namespace

void register_coalesce_matches_typescript_contract() {
  {
    const std::vector<IrOp> program = {store("1"), recall("1"), halt(), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 1,
            "register-coalesce did not rewrite non-overlapping direct live ranges");
    require(!std::any_of(result.ops.begin(), result.ops.end(),
                         [](const IrOp& op) {
                           return (op.kind == IrKind::Store || op.kind == IrKind::Recall) &&
                                  op.register_name == "2";
                         }),
            "register-coalesce left a direct R2 access after coalescing");
    require(result.optimizations.size() == 1,
            "register-coalesce did not report non-overlap optimization");
    require(result.optimizations.at(0).name == "register-coalesce",
            "register-coalesce reported wrong optimization name");
  }

  {
    const std::vector<IrOp> program = {store("1"), recall("1"), store("2"),
                                       store("1"), recall("2"), halt()};
    const std::map<std::string, std::string> plain =
        core::passes::compute_non_overlapping_register_mapping(program);
    const std::map<std::string, std::string> def_aware =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{.def_aware = true});

    require(plain.size() == 1,
            "register-coalesce plain mapping stopped matching TypeScript liveness behavior");
    require(def_aware.empty(),
            "register-coalesce def-aware mapping failed to block dead-store clobber");
  }

  {
    const std::vector<IrOp> program = {recall("1"), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 0, "register-coalesce ignored live-at-entry exclusion");
    require(result.ops.size() == program.size(), "register-coalesce changed live-at-entry program");
  }

  {
    const std::vector<IrOp> program = {
        store("0"), indirect_store("0", "4,5,6,7"), halt(), store("3"), recall("3")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{.def_aware = true});

    require(mapping.contains("3") && mapping.at("3") == "0",
            "def-aware register mapping did not reuse an indirect keep register with proved "
            "disjoint targets and live ranges");
  }

  {
    const std::vector<IrOp> program = {
        store("0"), indirect_store("0"), halt(), store("3"), recall("3")};
    const std::map<std::string, std::string> mapping =
        core::passes::compute_non_overlapping_register_mapping(
            program, core::passes::RegisterCoalesceMappingOptions{.def_aware = true});

    require(mapping.empty(),
            "def-aware register mapping accepted an unknown indirect-memory target set");
  }

  {
    IrOp display_store = store("2");
    display_store.meta.comment = "display rendered digit";
    IrOp display_recall = recall("2");
    display_recall.meta.comment = "display rendered digit";
    const std::vector<IrOp> program = {store("1"), recall("1"), halt(), display_store,
                                       display_recall};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 0,
            "register-coalesce ignored display-focus-sensitive register exclusion");
    require(result.ops.back().kind == IrKind::Recall && result.ops.back().register_name == "2",
            "register-coalesce rewrote display-focus-sensitive recall");
  }

  {
    CompileOptions options;
    options.coalesce_copies = true;
    const std::vector<IrOp> program = {recall("1"), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program, options);

    require(result.applied == 1, "copy-coalesce did not merge a sole non-diverging copy");
    require(result.ops.size() == 2, "copy-coalesce did not drop the redundant store");
    require(result.ops.at(0).kind == IrKind::Recall && result.ops.at(0).register_name == "1",
            "copy-coalesce changed source recall unexpectedly");
    require(result.ops.at(1).kind == IrKind::Recall && result.ops.at(1).register_name == "1",
            "copy-coalesce did not rewrite destination recall to source register");
    require(result.optimizations.size() == 1 && result.optimizations.at(0).name == "copy-coalesce",
            "copy-coalesce reported wrong optimization metadata");
  }

  {
    IrOp raw_store = store("1");
    raw_store.meta.raw = true;
    const std::vector<IrOp> program = {raw_store, recall("1"), halt(), store("2"), recall("2")};
    const core::passes::PassResult result = run_register_coalesce(program);

    require(result.applied == 0, "register-coalesce crossed a raw rewrite barrier");
    require(result.ops.size() == program.size(), "register-coalesce changed raw-barrier program");
  }
}

} // namespace mkpro::tests
