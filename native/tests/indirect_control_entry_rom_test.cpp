#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/opcodes.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"
#include <iostream>
using namespace mkpro; using mkpro::tests::require;
auto observe(int opcode,int reg,int mode,int value) {
 MachineEmitter e; const int source=reg==9?8:9;
 if(mode==0)e.emit_op(0x60+source);
 if(mode==1)e.emit_number(std::to_string(value));
 if(mode==2){e.emit_op(0x60+source);e.emit_op(0x0e);}
 if(mode==3){e.emit_number("8");e.emit_op(0x0c);e.emit_number("1");}
 if(mode==4){e.emit_number("8");e.emit_op(0x0c);e.emit_number("1");e.emit_op(0x0b);}
 e.emit_op(opcode);
 const int target=static_cast<int>(e.items.size());
 e.emit_number("9");e.emit_op(0x41);e.emit_stop(StopDisposition::Terminal);
 auto r=resolve_machine_items(e.items);require(r.diagnostics.empty(),"entry probe resolve");
 std::vector<int> bytes;for(auto& s:r.steps)bytes.push_back(s.opcode);
 emulator::MK61 c;require(c.load_program(bytes).diagnostics.empty(),"entry probe load");
 c.set_register("X","17");c.set_register("Y","23");c.set_register("Z","31");c.set_register("T","43");
 c.set_register(std::string(1,"0123456789abcde"[source]),std::to_string(value));
 c.set_register(std::string(1,"0123456789abcde"[reg]),std::to_string(target+(reg<=3?1:reg<=6?-1:0)));
 c.press_sequence({"В/О","С/П"});require(c.run_until_stable(1500,8).stopped,"entry probe stop");
 std::vector<std::string> out;for(const char* name:{"X","Y","Z","T"})out.push_back(c.read_register(name));return out;
}
int main(){try {int pass=0,close=0,mixed=0;
 for(int reg=0;reg<15;++reg)for(int base:{0x70,0x80,0x90,0xc0,0xe0}) {
   bool p=true,c=true;
   for(int mode=0;mode<5;++mode)for(int value:{-1,0,1}) {
    auto actual=observe(base+reg,reg,mode,value);auto closed=observe(0x42,reg,mode,value);auto preserved=observe(0x54,reg,mode,value);
    require(actual==closed,"enabled entry disabled at opcode "+std::to_string(base+reg));
    p=p && actual==preserved;c=c && actual==closed;
   }
   pass+=p;close+=c;mixed+=!p && !c;
 }
 std::cout<<"indirect flow automatic-entry facts: preserves="<<pass<<" closes="<<close<<" mixed="<<mixed<<"; never disables enabled lift\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
