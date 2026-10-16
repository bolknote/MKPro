#include <algorithm>
#include "mkpro/core/passes/semantic_region_folding.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"
#include <iostream>
using namespace mkpro;using namespace mkpro::core;using namespace mkpro::core::passes;
using mkpro::tests::require;
std::vector<IrOp> fixture(bool open=false,bool repeated=false,bool store=true,bool thunk=false) {
 MachineEmitter e; e.emit_op(0x0d);e.emit_number("2");if(!open)e.emit_op(0x49);
 e.emit_jump(0x53,"call","setup");e.emit_op(0x42);
 if(repeated) e.emit_jump(0x53,"call","setup");
 e.emit_number("37");e.emit_jump(0x53,"call","shared");e.emit_op(0x43);
 e.emit_stop(StopDisposition::Terminal);
 e.emit_label("setup");e.emit_number("59");if(thunk)e.emit_jump(0x51,"tail","shared");e.emit_label("shared");
 if(store)e.emit_op(0x4a);else e.emit_op(0x54);
 e.emit_op(0x60);e.emit_op(0x10);e.emit_op(0x52);
 return raise_machine_to_ir(e.items);
}

int position(const std::vector<IrOp>& ops,const std::string& name){int pc=0;for(const auto& op:ops){if(op.kind==IrKind::Label && op.name==name)return pc;pc+=cells_per_op(op);}throw std::runtime_error("label missing");}
std::vector<IrOp> indirect_fixture(){
 MachineEmitter e;e.emit_op(0x0d);e.emit_number("2");e.emit_op(0x49);
 e.emit_op(0xed);e.items.back().indirect_flow_targets=std::vector<IrTarget>{"call_site"};
 e.emit_label("call_site");e.emit_op(0xac);e.items.back().indirect_flow_targets=std::vector<IrTarget>{"setup"};e.emit_op(0x42);
 e.emit_number("37");e.emit_jump(0x53,"call","shared");e.emit_op(0x43);e.emit_stop(StopDisposition::Terminal);
 e.emit_number("5");e.emit_op(0x52);
 e.emit_label("setup");e.emit_number("59");e.emit_label("shared");e.emit_op(0x4a);e.emit_op(0x60);e.emit_op(0x10);e.emit_op(0x52);
 return raise_machine_to_ir(e.items);
}

auto observe(const std::vector<IrOp>& ops,const std::string& value) {
 auto r=resolve_machine_items(lower_ir_to_machine(ops));require(r.diagnostics.empty(),"resolve");
 std::vector<int> bytes;for(auto& s:r.steps)bytes.push_back(s.opcode);
 emulator::MK61 c;require(c.load_program(bytes).diagnostics.empty(),"load");
 for(const char* reg:{"X","Y","Z","T","X1","0"})c.set_register(reg,value);
 if(std::any_of(ops.begin(),ops.end(),[](const auto& op){return op.kind==IrKind::IndirectCall;})){c.set_register("c",std::to_string(position(ops,"setup")));c.set_register("d",std::to_string(position(ops,"call_site")));}
 c.press_sequence({"В/О","С/П"});require(c.run_until_stable(3000,8).stopped,"return");
 std::vector<std::string> o;for(const char* reg:{"X","Y","Z","T","X1","0","1","2","3","4","5","6","7","8","9","a","b","c","d","e"})o.push_back(c.read_register(reg));
 c.press(".");o.push_back(c.display_text(true));return o;
}
int main(){try {CompileOptions o;auto a=fixture();auto b=distribute_constant_call_prefix(a,PassContext{o},{});
 require(b.applied>0,"positive prefix should distribute");require(lower_ir_to_machine(a).size()==lower_ir_to_machine(b.ops).size(),"neutral size");
 for(const std::string seed:{"0","-7","3.25","8.EEEEEEE","1E99"})require(observe(a,seed)==observe(b.ops,seed),"ROM stack/register/X2 equality "+seed);
 auto t=fixture(false,false,true,true);auto u=distribute_constant_call_prefix(t,PassContext{o},{});require(u.applied>0,"tail prefix distributes");for(const std::string seed:{"0","-7","3.25","8.EEEEEEE","1E99"})require(observe(t,seed)==observe(u.ops,seed),"tail ROM equality "+seed);
 for(auto ops:{fixture(true),fixture(false,true),fixture(false,false,false)})require(distribute_constant_call_prefix(ops,PassContext{o},{}).applied==0,"negative must reject");
 auto ia=indirect_fixture();auto ib=distribute_constant_call_prefix(ia,PassContext{o},{});require(ib.applied>0,"indirect prefix with proved lift should distribute");
 auto placed=layout_symbolic_regions(ib.ops,{{"setup",position(ia,"setup")},{"call_site",position(ia,"call_site")}},PassContext{o},4096,nullptr,true);require(placed.has_value(),"immutable data/flow selector stays at its old target");
 for(const std::string seed:{"0","-7","3.25","8.EEEEEEE","1E99"})require(observe(ia,seed)==observe(placed->ops,seed),"indirect prefix must preserve all registers/stack/X2 with identical selector values "+seed);
 auto annotated=fixture(); for(auto& op:annotated) if(op.kind==IrKind::Store && op.opcode==0x4a)op.meta.comment="callee-hole charge-entry store; proof=synthetic; selector=a; entry-repair=sunk-literal";
 auto moved=distribute_constant_call_prefix(annotated,PassContext{o},{});require(moved.applied>0,"annotated prefix distributes");
 require(std::any_of(moved.ops.begin(),moved.ops.end(),[](const auto& op){return op.kind==IrKind::Call && op.meta.comment && op.meta.comment->starts_with("callee-hole charge-entry call; proof=synthetic; selector=a;");}),"call receives the existing charge-store protocol identity");
 for(const std::string seed:{"0","-7","3.25","8.EEEEEEE","1E99"})require(observe(annotated,seed)==observe(moved.ops,seed),"annotated ROM equality "+seed);
 std::cout<<"constant prefix and protocol transport ROM contracts passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
