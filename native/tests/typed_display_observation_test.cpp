#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/passes/liveness_analysis.hpp"
#include "mkpro/emulator/mk61.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace mkpro; using namespace mkpro::core;
void require(bool v,const std::string& m){if(!v)throw std::runtime_error(m);}
std::vector<MachineItem> protocol(bool last_x=false,bool typed=true,bool prefix=false,bool short_prefix=false) {
 MachineEmitter e;
 if(prefix){ if(short_prefix)e.emit_op(0x66); else{e.emit_op(0x64);e.emit_op(0x65);e.emit_op(0x10);} }
 e.emit_label("prompt");
 e.next_op_manual_interaction=ManualInteractionAnchor{7,-1,ManualInteractionAnchorKind::PromptStop};
 e.emit_stop(StopDisposition::Resumable);
 if(!typed)e.items.back().roles.clear();
 e.next_op_manual_interaction=ManualInteractionAnchor{7,0,ManualInteractionAnchorKind::SingleStepCommand};e.emit_op(0x41);
 e.next_op_manual_interaction=ManualInteractionAnchor{7,1,ManualInteractionAnchorKind::ContinuousResume};e.emit_op(0x42);
 if(last_x){e.emit_op(0x0f);e.emit_op(0x40);}
 e.emit_op(0x61);e.emit_op(0x62);e.emit_op(0x10);e.emit_op(0x43);
 e.emit_op(0x62);e.emit_op(0x63);e.emit_stop(StopDisposition::Terminal);
 return e.items;
}
bool proof(const std::vector<MachineItem>& m,StackValueEqualityState s,int address=0){auto f=build_post_layout_control_flow(m);require(f.proved,"protocol CFG");return prove_post_layout_stack_entry_equality(m,f,address,s);}
std::vector<std::string> run(bool shortened,bool last_x){
 auto r=resolve_machine_items(protocol(last_x,true,true,shortened));require(r.diagnostics.empty(),"ROM resolve");std::vector<int> code;for(const auto& x:r.steps)code.push_back(x.opcode);
 emulator::MK61 c;require(c.load_program(code).diagnostics.empty(),"ROM load");
 c.set_register("4","2");c.set_register("5","3");c.set_register("6","5");c.set_register("X1","11");
 c.press_sequence({"\u0412/\u041e","\u0421/\u041f"});require(c.run_until_stable(3000,8).stopped,"prompt stop");
 std::vector<std::string> out{c.display_text(true),c.read_register("Y")};
 c.input_number("3",true);c.press("\u041f\u041f");require(c.run_until_stable(3000,8).stopped,"single step");out.push_back(c.read_register("1"));
 c.input_number("7",true);c.press("\u0421/\u041f");require(c.run_until_stable(3000,8).stopped,"resumed stop");out.push_back(c.display_text(true));out.push_back(c.read_register("Y"));out.push_back(c.read_register("3"));if(last_x)out.push_back(c.read_register("0"));return out;
}
int main(){try{
 StackValueEqualityState hidden{{true,true,true,true},false,false};
 require(proof(protocol(),hidden),"dead hidden state should cross typed prompt");
 require(!proof(protocol(true),hidden),"future last-X read must reject");
 require(!proof(protocol(false,false),hidden),"untyped prompt must reject");
 auto visible=hidden;visible.stack_equal[0]=false;require(!proof(protocol(),visible),"visible X difference must reject");visible=hidden;visible.stack_equal[1]=false;require(!proof(protocol(),visible),"preview Y difference must reject");
 require(run(false,false)==run(true,false),"ROM documented interface and subsequent reads differ");
 require(run(false,true)!=run(true,true),"ROM must expose last-X counterexample");
 MachineEmitter e;e.emit_stop(StopDisposition::Terminal);require(proof(e.items,hidden),"typed terminal hides unused state");e.items.back().roles.clear();require(!proof(e.items,hidden),"untyped terminal remains barrier");
 MachineEmitter exponent;exponent.emit_op(0x0c);exponent.emit_stop(StopDisposition::Terminal);
 require(!proof(exponent.items,hidden),"exponent entry must not read a differing X2");
 std::cout<<"typed observation and future-use ROM contracts passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
