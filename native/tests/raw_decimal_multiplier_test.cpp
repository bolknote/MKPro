#include "mkpro/core/raw_bcd_word.hpp"
#include "mkpro/emulator/mk61.hpp"
#include "test_support.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
using mkpro::tests::require;
struct Product {
  std::array<std::string,4> stack;
  std::string display, pc, last_x;
  bool stopped = false;
};
Product product(const std::string& value,const std::string& factor,bool reverse=false) {
  mkpro::emulator::MK61 calc;
  std::vector<int> code{0x61,0x62};
  if(reverse)code.push_back(0x14);
  code.insert(code.end(),{0x12,0x50});
  require(calc.load_program(code).diagnostics.empty(),"raw scale fact loads");
  calc.set_register("1",value);calc.set_register("2",factor);
  calc.set_register("Y","37");calc.set_register("Z","41");calc.set_register("T","53");
  calc.press_sequence({"\u0412/\u041e","\u0421/\u041f"});
  const auto stopped=calc.run_until_stable(5000,8);
  Product result{{},calc.display_text(),calc.program_counter(),{},stopped.stopped};
  std::size_t at=0;
  for(const auto name:{"X","Y","Z","T"})result.stack[at++]=calc.read_register(name);
  result.last_x=calc.read_register("X1");
  return result;
}
void run() {
  using namespace mkpro::core;
  for(const auto word:{"EE-2","FE-2","1DE-2","D.1E-2","DE-5","DE0","invalid"})
    require(!evaluate_raw_right_decimal_scale(word),"unsupported raw words never acquire the decimal scale law");
  std::size_t checked=0;
  for(const auto mantissa:{"0","1","1.0000001","1.2345678","1.9999999","5.5555555","9.0000001","9.9999999"})
    for(const int exponent:{-99,-98,-1,0,1,98,99})
      for(const char digit:{'A','B','C','D'})
        for(const int order:{-4,-3,-2,-1})
          for(const bool left_negative:{false,true})
            for(const bool right_negative:{false,true}) {
              if(left_negative && std::string(mantissa)=="0")continue;
              const auto left=(left_negative?std::string("-"):"")+mantissa+"E"+std::to_string(exponent);
              const auto raw=(right_negative?std::string("-"):"")+digit+"E"+std::to_string(order);
              const auto decimal=(right_negative?std::string("-"):"")+"1E"+std::to_string(order+1);
              const auto law=evaluate_raw_right_decimal_scale(raw);
              require(law && law->exponent==order+1 && law->negative==right_negative,
                  "the scale is derived from the word, not a selected game constant");
              const auto a=product(left,raw),b=product(left,decimal);
              require(a.stopped && b.stopped && a.stack==b.stack && a.display==b.display && a.pc==b.pc,
                  "ordinary mantissa/sign/order/zero boundaries agree for "+left+" * "+raw);
              ++checked;
            }
  require(checked==3360U,"the complete declared ROM boundary matrix runs");
  const auto raw=product("1","DE-2"),decimal=product("1","1E-1");
  require(raw.last_x!=decimal.last_x,"the arithmetic law does not identify physical last-X");
  require(product("1","DE-2",true).stack!=product("1","1E-1",true).stack,
      "reversing the operands is deliberately outside the law");
}
}
int main(){try{run();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
