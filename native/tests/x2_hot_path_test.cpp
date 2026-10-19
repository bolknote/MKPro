#include "mkpro/core/detail/decimal_mantissa_syntax.hpp"
#include "mkpro/core/detail/basic_opcode_ir_kind.hpp"
#include "mkpro/core/ir.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/core/shared_helper_continuation.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace mkpro;
using namespace mkpro::core;
using tests::require;

void helper_batch_equivalence() {
  const auto op = [](int code) { return MachineItem::op(code, opcode_by_code(code).name); };
  for (int mode=0;mode<6;++mode) {
    std::vector<MachineItem> items{
        MachineItem::label("entry_a"),op(0x53),MachineItem::address(std::string{"worker"}),op(0x10),op(0x40),op(0x52),
        MachineItem::label("entry_b"),op(0x53),MachineItem::address(std::string{"worker"}),op(0x10),op(0x40),op(0x52),
        MachineItem::label("entry_c"),op(0x53),MachineItem::address(std::string{"worker"}),op(0x12),op(0x52),
        MachineItem::label("worker"),op(0x22),op(0x52)};
    if(mode==1)items.push_back(MachineItem::label("worker"));
    if(mode==2)items[2].target=std::string{"missing_target"};
    if(mode==3)items.push_back(op(0x87)); // Unproved unrelated indirect flow.
    if(mode==5){items.push_back(MachineItem::label("fourth_call"));items.push_back(op(0x53));
      items.push_back(MachineItem::address(std::string{"worker"}));items.push_back(op(0x52));}
    SharedHelperContinuationOptions options;
    if(mode==4)options.maximum_convergence_states=0;
    std::set<std::string> labels;
    for(const auto& item:items)if(item.kind==MachineItemKind::Label)labels.insert(item.name);
    std::vector<SharedHelperContinuationProof> scalar,batch;
    for(const auto& label:labels)scalar.push_back(verify_shared_helper_continuation(items,label,options));
    visit_shared_helper_continuations(items,options,[&](auto proof){batch.push_back(std::move(proof));return true;});
    require(scalar==batch,"batch helper preparation retains every scalar proof field, reason and label order");
    SharedHelperContinuationProof expected;
    for(const auto& proof:scalar){
      if(proof.proved){expected=proof;break;}
      if(expected.reasons.empty())expected=proof;
    }
    require(find_shared_helper_continuation(items,options)==expected,
            "batch discovery keeps the same first successful proof or rejection");
    if(mode==0)require(expected.proved,"batch differential fixture includes a fully proved continuation");
    std::size_t visited=0;
    visit_shared_helper_continuations(items,options,[&](auto proof){
      require(proof==scalar.front(),"early visitor receives the first exact scalar result");
      ++visited;return false;});
    require(visited==1,"batch discovery never proves later labels after an early stop");
    visit_shared_helper_continuations(items,options,{});
    require(!verify_shared_helper_continuation(items,"absent_label",options).proved,
            "the scalar API keeps its absent-label failure before global relocation");
  }
}

void opcode_kinds() {
  for (int pass = 0; pass < 2; ++pass)
    for (int opcode = 0; opcode <= 255; ++opcode) {
      const auto original = raise_machine_to_ir(
          {MachineItem::op(opcode, opcode_by_code(opcode).name)});
      const auto expected = original.empty() ? IrKind::Plain : original.front().kind;
      require(detail::basic_opcode_ir_kind(opcode) == expected,
              "cold and warm opcode classification matches the original IR raiser");
    }
}

void decimal_syntax() {
  const std::regex original(R"(^(-?)([0-9]{1,8})(?:\.([0-9]*))?$)");
  const auto check = [&](const std::string& text) {
    std::smatch matched;
    const bool syntax = std::regex_match(text, matched, original);
    const auto digits = syntax ? matched[2].length() + matched[3].length() : 0;
    require(detail::is_plain_decimal_mantissa(text) == (syntax && digits <= 8),
            "ASCII entry scanner must exactly match the previous complete regex and digit bound");
  };
  std::vector<std::string> frontier{std::string{}};
  for (int length = 0; length <= 5; ++length) {
    std::vector<std::string> next;
    for (const auto& text : frontier) {
      check(text);
      if (length != 5)
        for (const char symbol : std::string_view{"-01. e+"})
          next.push_back(text + symbol);
    }
    frontier = std::move(next);
  }
  for (std::size_t integer = 0; integer <= 10; ++integer)
    for (std::size_t fraction = 0; fraction <= 10; ++fraction)
      for (const std::string& sign : {std::string{}, std::string{"-"}}) {
        check(sign + std::string(integer, '0') + "." + std::string(fraction, '9'));
        check(sign + std::string(integer, '9'));
      }
  for (const std::string text : {"-0", "-0.", "00000000", "12345678.",
                                 "1.2345678", "1.23456789", "1e2", "1E-2",
                                 "0\n", "1,2", "8.ABCDEF0", "--1"})
    check(text);
  check(std::string{"1\0", 2});
  check("\xD9\xA1");
  check("\xE2\x88\x92" "1");
}

IrOp command(int opcode) {
  std::vector<MachineItem> image{MachineItem::op(opcode, opcode_by_code(opcode).name)};
  if (opcode_by_code(opcode).takes_address)
    image.push_back(MachineItem::address(0));
  return raise_machine_to_ir(image).front();
}

void may_target_store_precision() {
  using namespace passes;
  auto state=empty_x2_value_dataflow_state(true);
  int at=0;
  for(const int code : {5,0x49,2,0x47,3,0x48,8,0x40,0x67,4,2})
    state=*transfer_x2_value_state_for_edge(state,command(code),X2DataflowEdgeKind::Normal,
                                           {.track_register_memory=true},at++);
  require(state.memory && state.memory->contains("9") && state.shapeMemory && state.shapeMemory->contains("9"),
          "may-target fixture has a grounded unrelated bank value and shape");
  const auto before=state;
  for(int mode=0;mode<7;++mode){
    auto store=command(0xb0);
    if(mode==0)store.meta.indirect_memory_targets=std::vector<int>{7,8};
    if(mode==1)store.meta.indirect_memory_targets=std::vector<int>{0,7};
    if(mode==3)store.meta.indirect_memory_targets=std::vector<int>{};
    if(mode==4)store.meta.indirect_memory_targets=std::vector<int>{7,15};
    if(mode==5)store.meta.indirect_memory_targets=std::vector<int>{7,8,7};
    if(mode==6)store.meta.indirect_memory_targets=std::vector<int>{-1,7};
    const auto output=transfer_x2_value_state_for_edge(state,store,X2DataflowEdgeKind::Normal,
                                                     {.track_register_memory=true},at);
    require(output && state==before,"may-target transfer does not mutate its borrowed input");
    const bool complete=mode==0 || mode==1 || mode==5;
    require(output->memory && output->shapeMemory && output->memory->contains("9")==complete &&
            output->shapeMemory->contains("9")==complete,
            "only a complete supported target set retains unrelated value and shape memory");
    require(!output->memory->contains("0") && !output->memory->contains("7"),
            "selector mutation and every possible written bank lose their stale aliases");
    if(complete){
      const auto recalled=transfer_x2_value_state_for_edge(*output,command(0x69),X2DataflowEdgeKind::Normal,
                                                          {.track_register_memory=true},at+1);
      require(recalled && recalled->x.contains("decimal:5:normalized"),
              "the unaffected bank remains available to later exact-word proofs");
    }else require(output->memory->empty() && output->shapeMemory->empty(),
                 "unknown/empty/Rf/invalid may-target sets retain the historical full kill");
  }
  for(const int selector : {8,9}){
    std::vector<int> code{5,0x49,2,0x47,3,0x48,selector,0x40,0x67,4,2,0xb0,0x69,0x50};
    emulator::MK61 calc;
    require(calc.load_program(code).diagnostics.empty(),"may-target fact fixture fits stock ROM");
    calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
    require(calc.run_until_stable(5000,8).stopped && std::stod(calc.read_register("X"))==5 &&
            std::stod(calc.read_register("9"))==5 && std::stod(calc.read_register(selector==8?"7":"8"))==42,
            "ROM covers both may-target destinations without changing the retained bank");
  }
}

void borrowed_transfer() {
  using namespace passes;
  const std::vector<std::vector<int>> prefixes{
      {}, {1, 2}, {1, 0x0a, 2}, {1, 0x0c, 2, 0x0b},
      {8, 0x0a, 1, 0x0e, 3, 0x38, 0x48, 0x68},
      {5, 0x0e, 8, 0x38, 0x3a}, {0, 0x0b, 0x0c},
      {0x69, 0x0e, 0x34, 0x0f}};
  {
    auto zero = empty_x2_value_dataflow_state();
    for (const int code : {0, 0x0a})
      zero = *transfer_x2_value_state_for_edge(
          zero, command(code), X2DataflowEdgeKind::Normal);
    require(zero.x.contains("decimal:0:normalized"),
            "a trailing entry point retains the normalized zero fact");
  }
  const std::vector<int> codes{0, 7, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x21, 0x22, 0x25, 0x30,
      0x31, 0x32, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
      0x48, 0x52, 0x57, 0x59, 0x5c, 0x5e, 0x68, 0x78, 0xa8, 0xb0, 0xd0};
  for (const auto& prefix : prefixes) {
    auto state = empty_x2_value_dataflow_state(true);
    int position = 0;
    for (const int code : prefix) {
      const auto next = transfer_x2_value_state_for_edge(
          std::optional{state}, command(code), X2DataflowEdgeKind::Normal,
          {.track_register_memory = true}, position++);
      require(next.has_value(), "test entry prefix has a complete abstract state");
      state = *next;
    }
    const auto before = state;
    for (const int code : codes)
      for (const auto edge : {X2DataflowEdgeKind::Normal,
                              X2DataflowEdgeKind::Fallthrough, X2DataflowEdgeKind::Jump})
        for (const bool memory : {false, true})
          for (const bool vp : {false, true}) {
            const X2TransferStateOptions options{
                .track_register_memory = memory, .target_starts_with_vp = vp};
            const auto op = command(code);
            const auto owned = transfer_x2_value_state_for_edge(std::optional{state}, op, edge, options, 17);
            const auto borrowed = transfer_x2_value_state_for_edge(state, op, edge, options, 17);
            require(owned == borrowed && state == before,
                    "borrowed input preserves every optional, shape, memory, entry and VP field without mutation");
            if (owned) {
              require(same_x2_value_dataflow_state(state, *owned) ==
                          same_x2_value_dataflow_state(std::optional{state}, std::optional{*owned}) &&
                          same_x2_value_dataflow_state(std::optional{state}, *owned) ==
                              same_x2_value_dataflow_state(state, std::optional{*owned}),
                      "all borrowed comparison overloads retain the legacy semantic equality");
              require(!same_x2_value_dataflow_state(std::nullopt, *owned) &&
                          !same_x2_value_dataflow_state(*owned, std::nullopt),
                      "absent comparisons remain distinct from a present empty state");
              require(join_x2_value_dataflow_states(&state, *owned, memory) ==
                          join_x2_value_dataflow_states(std::optional{state}, *owned, memory) &&
                          join_x2_value_dataflow_states(nullptr, *owned, memory) ==
                              join_x2_value_dataflow_states(std::nullopt, *owned, memory),
                      "borrowed and optional joins retain every field and bottom-state memory projection");
            }
            require(!transfer_x2_value_state_for_edge(std::nullopt, op, edge, options, 17),
                    "absent input never creates an abstract state");
          }
  }
}
} // namespace

int main() {
  try {
    helper_batch_equivalence();
    opcode_kinds();
    decimal_syntax();
    may_target_store_precision();
    borrowed_transfer();
    std::cout << "X2 hot path: exact syntax and borrowed transfer passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
