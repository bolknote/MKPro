#include "mkpro/core/passes/cfg.hpp"

#include "test_support.hpp"

#include <optional>
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
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = std::move(register_name);
  op.opcode = 0x40;
  op.meta.mnemonic = "X->П " + op.register_name;
  return op;
}

IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

IrOp jump(IrTarget target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.target = std::move(target);
  op.opcode = 0x51;
  op.meta.mnemonic = "БП";
  return op;
}

IrOp cjump(IrTarget target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.target = std::move(target);
  op.opcode = 0x5e;
  op.meta.mnemonic = "F x=0";
  return op;
}

IrOp call(IrTarget target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.target = std::move(target);
  op.opcode = 0x53;
  op.meta.mnemonic = "ПП";
  return op;
}

IrOp loop(std::string target) {
  IrOp op;
  op.kind = IrKind::Loop;
  op.counter = "L0";
  op.target = std::move(target);
  op.opcode = 0x5d;
  op.meta.mnemonic = "F L0";
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

IrOp indirect_jump(std::string register_name, std::optional<int> target = std::nullopt) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = std::move(register_name);
  op.opcode = 0x80;
  op.meta.mnemonic = "К БП " + op.register_name;
  if (target.has_value())
    op.meta.comment = "indirect-target=" + std::to_string(*target);
  return op;
}

IrOp indirect_call(std::string register_name, std::optional<int> target = std::nullopt) {
  IrOp op;
  op.kind = IrKind::IndirectCall;
  op.register_name = std::move(register_name);
  op.opcode = 0xa0;
  op.meta.mnemonic = "К ПП " + op.register_name;
  if (target.has_value())
    op.meta.comment = "indirect-target=" + std::to_string(*target);
  return op;
}

bool edges_equal(const std::vector<core::passes::CfgEdge>& actual,
                 const std::vector<std::pair<int, core::passes::CfgEdgeKind>>& expected) {
  if (actual.size() != expected.size())
    return false;
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (actual.at(index).target != expected.at(index).first ||
        actual.at(index).kind != expected.at(index).second) {
      return false;
    }
  }
  return true;
}

void require_edges(const std::vector<std::vector<core::passes::CfgEdge>>& edges, std::size_t index,
                   const std::vector<std::pair<int, core::passes::CfgEdgeKind>>& expected,
                   const std::string& context) {
  require(index < edges.size(), context + " edge index exists");
  require(edges_equal(edges.at(index), expected), context + " edges should match TypeScript");
}

} // namespace

void cfg_matches_typescript_contract() {
  using core::passes::build_cfg_edges;
  using core::passes::build_cfg_successors;
  using core::passes::build_control_flow_graph;
  using core::passes::build_target_indexes;
  using core::passes::BuildCfgOptions;
  using core::passes::CfgEdgeKind;
  using core::passes::CfgUncertaintyKind;
  using core::passes::loop_counter_register;

  {
    const std::vector<IrOp> ops = {
        plain(0x01, "1"),
        label("head"),
        jump(std::string("head")),
        plain(0x02, "2"),
    };
    const core::passes::CfgTargetIndexes indexes = build_target_indexes(ops);
    require(indexes.label_index.at("head") == 1, "cfg labels should map to op indexes");
    require(indexes.address_index.at(0) == 0, "cfg address 0 should map to first op");
    require(indexes.address_index.at(1) == 2, "cfg address 1 should map to jump op");
    require(indexes.address_index.at(3) == 3, "cfg address 3 should map to last op");
    require(!indexes.address_index.contains(2), "cfg should not map address operands as op starts");
  }

  {
    const std::vector<IrOp> ops = {plain(0x01, "1"), label("x"), store("4"), halt(),
                                   plain(0x02, "2")};
    const std::vector<std::vector<core::passes::CfgEdge>> edges = build_cfg_edges(ops);
    require_edges(edges, 0, {{1, CfgEdgeKind::Fallthrough}}, "plain fallthrough");
    require_edges(edges, 1, {{2, CfgEdgeKind::Fallthrough}}, "label fallthrough");
    require_edges(edges, 2, {{3, CfgEdgeKind::Fallthrough}}, "store fallthrough");
    require_edges(edges, 3, {{4, CfgEdgeKind::Fallthrough}}, "stop resume fallthrough");
    require_edges(edges, 4, {}, "terminal plain");
  }

  {
    const std::vector<IrOp> ops = {cjump(std::string("done")), plain(0x01, "1"),
                                   jump(std::string("done")), label("done"), halt()};
    const std::vector<std::vector<core::passes::CfgEdge>> edges = build_cfg_edges(ops);
    require_edges(edges, 0, {{3, CfgEdgeKind::Jump}, {1, CfgEdgeKind::Fallthrough}},
                  "conditional jump");
    require_edges(edges, 2, {{3, CfgEdgeKind::Jump}}, "direct jump");
  }

  {
    const std::vector<IrOp> ops = {label("head"), plain(0x01, "1"), loop("head"), halt()};
    require_edges(build_cfg_edges(ops), 2, {{0, CfgEdgeKind::Jump}, {3, CfgEdgeKind::Fallthrough}},
                  "counted loop");
  }

  {
    const std::vector<IrOp> ops = {jump(3), plain(0x01, "1"), plain(0x02, "2")};
    require_edges(build_cfg_edges(ops), 0, {{2, CfgEdgeKind::Jump}}, "numeric target");
  }

  {
    const std::vector<IrOp> ops = {call(std::string("helper")), halt(), label("helper"),
                                   plain(0x01, "1"), ret()};
    const std::vector<std::vector<core::passes::CfgEdge>> edges = build_cfg_edges(ops);
    require_edges(edges, 0, {{2, CfgEdgeKind::Jump}}, "direct call target");
    require_edges(edges, 4, {{1, CfgEdgeKind::Normal}}, "direct return continuation");
  }

  {
    const std::vector<IrOp> ops = {indirect_jump("7", 3), indirect_jump("8"), plain(0x01, "1"),
                                   plain(0x02, "2")};
    const core::passes::ControlFlowGraph graph = build_control_flow_graph(ops);
    require_edges(graph.edges, 0, {{3, CfgEdgeKind::Jump}}, "proved indirect jump");
    require_edges(graph.edges, 1,
                  {{0, CfgEdgeKind::Jump},
                   {1, CfgEdgeKind::Jump},
                   {2, CfgEdgeKind::Jump},
                   {3, CfgEdgeKind::Jump}},
                  "conservative unknown indirect jump");
    require(graph.uncertainties.size() == 1U && graph.uncertainties.front().source == 1 &&
                graph.uncertainties.front().kind == CfgUncertaintyKind::UnknownIndirectTarget,
            "cfg did not expose the unknown indirect target proof gap");

    const std::vector<std::vector<core::passes::CfgEdge>> sparse =
        build_cfg_edges(ops, BuildCfgOptions{.unknown_indirect_flow_to_all = false});
    require_edges(sparse, 1, {}, "explicit sparse unknown indirect jump");
  }

  {
    IrOp dispatch = indirect_jump("7");
    dispatch.meta.indirect_flow_targets = std::vector<IrTarget>{IrTarget{1}, IrTarget{2}};
    const std::vector<IrOp> ops = {dispatch, plain(0x01, "1"), plain(0x02, "2")};
    const core::passes::ControlFlowGraph graph = build_control_flow_graph(ops);

    require_edges(graph.edges, 0, {{1, CfgEdgeKind::Jump}, {2, CfgEdgeKind::Jump}},
                  "typed multi-target indirect jump");
    require(graph.targets_are_exact(),
            "resolved typed multi-target indirect jump was marked uncertain");
  }

  {
    const std::vector<IrOp> ops = {jump(std::string("missing")), plain(0x01, "1")};
    const core::passes::ControlFlowGraph graph = build_control_flow_graph(ops);

    require_edges(graph.edges, 0, {{0, CfgEdgeKind::Jump}, {1, CfgEdgeKind::Jump}},
                  "conservative unresolved direct jump");
    require(graph.uncertainties.size() == 1U &&
                graph.uncertainties.front().kind == CfgUncertaintyKind::UnresolvedDirectTarget,
            "cfg did not expose the unresolved direct target proof gap");
  }

  {
    const std::vector<IrOp> ops = {indirect_call("7", 2), halt(), plain(0x01, "1"), ret()};
    const std::vector<std::vector<core::passes::CfgEdge>> without = build_cfg_edges(ops);
    require_edges(without, 0, {{2, CfgEdgeKind::Jump}}, "indirect call without fallthrough");
    require_edges(without, 3, {{1, CfgEdgeKind::Normal}}, "indirect return continuation");

    const std::vector<std::vector<core::passes::CfgEdge>> with =
        build_cfg_edges(ops, BuildCfgOptions{.indirect_call_fallthrough = true});
    require_edges(with, 0, {{2, CfgEdgeKind::Jump}, {1, CfgEdgeKind::Fallthrough}},
                  "indirect call with fallthrough");
  }

  {
    const std::vector<IrOp> ops = {cjump(std::string("done")), plain(0x01, "1"), label("done"),
                                   halt()};
    require(build_cfg_successors(ops) == std::vector<std::vector<int>>{{2, 1}, {2}, {3}, {}},
            "cfg plain successors should drop edge kinds");
  }

  require(loop_counter_register("L0") == "0", "L0 should map to R0");
  require(loop_counter_register("L1") == "1", "L1 should map to R1");
  require(loop_counter_register("L2") == "2", "L2 should map to R2");
  require(loop_counter_register("L3") == "3", "L3 should map to R3");
}

} // namespace mkpro::tests
