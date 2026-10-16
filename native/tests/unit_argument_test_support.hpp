#pragma once
#include "mkpro/core/passes/finite_mode_representation.hpp"
#include "mkpro/core/emit/machine_emitter.hpp"
#include "mkpro/core/post_layout_control_flow.hpp"
#include "mkpro/emulator/mk61.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace mkpro;
using namespace mkpro::core;
void require(bool okay,const std::string& message) {if(!okay)throw std::runtime_error(message);}
void append(std::vector<MachineItem>& m,std::initializer_list<int> ops) {
  for(int op:ops)m.push_back(MachineItem::op(op,"unit input fixture"));
}
void target(std::vector<MachineItem>& m,int op,const char* label) {
  append(m,{op}); m.push_back(MachineItem::address(std::string(label)));
}
std::vector<MachineItem> fixture(bool negative,bool lastx=false,bool y=false,bool nonunit=false,bool reversed=false) {
  std::vector<MachineItem> m;
  m.push_back(MachineItem::label("main"));
  if(negative) {
    append(m,{0x69,0x69,0x61,0x38,0x49});
    if(reversed)append(m,{0x14});
    append(m,{0x11});target(m,0x57,"done");
    append(m,{1,0x0b});target(m,0x53,"worker");
  } else {
    append(m,{0x61,0x62,0x37,0x4a,1});target(m,0x53,"worker");
  }
  append(m,{0x61,0x62,0x38,0x4a,nonunit?2:1});target(m,0x53,"worker");
  if(negative)target(m,0x51,"main");
  m.push_back(MachineItem::label("done"));append(m,{0x50});m.back().stop_disposition=StopDisposition::Terminal;
  m.push_back(MachineItem::label("worker"));append(m,{0x48});
  if(lastx)append(m,{0x0f,0x47});
  if(y)append(m,{0x14,0x47});
  append(m,{0x63,0x64,0x65,0x66,0x10,0x68,0x10,0x40,0x52});
  return m;
}
std::vector<PreloadReport> preloads() {
  std::vector<PreloadReport> p;
  for(int i=0;i<15;++i) p.push_back({.register_name=std::string(1,"0123456789abcde"[i]),.value=std::to_string(i+2)});
  p[9].value="0";
  return p;
}
std::vector<passes::PassResult> candidates(const std::vector<MachineItem>& m,const std::vector<PreloadReport>& p) {
  CompileOptions o; o.defer_return_suffix_until_callee_hole=true;
  auto deferred=passes::deferred_finite_mode_representation_candidates(raise_machine_to_ir(m),{o},{.maximum_candidates=16,.allow_target_relayout=true,.runtime_preloads=&p,.normalize_unit_arguments=true});
  std::vector<passes::PassResult> c;for(auto& d:deferred) if(std::any_of(d.transformation.optimizations.begin(),d.transformation.optimizations.end(),[](const auto& r){return r.name=="unit-argument-producer-normalization";})) c.push_back(std::move(d.transformation));return c;
}
std::vector<std::string> observe(const std::vector<MachineItem>& m,std::vector<PreloadReport> p,const char* payload) {
  const auto r=resolve_machine_items(m);require(r.diagnostics.empty(),"resolve fixture");
  emulator::MK61 calc;std::vector<int> codes;
  for(const auto& step:r.steps)codes.push_back(step.opcode);
  require(calc.load_program(codes).diagnostics.empty(),"fit stock ROM");
  p[1].value=payload;
  for(const auto& v:p)calc.set_register(v.register_name,v.value);
  calc.press_sequence({"В/О","С/П"});require(calc.run_until_stable(6000,8).stopped,"stop");
  std::vector<std::string> out{calc.display_text(true)};
  for(const char* name:{"X","Y","Z","T","X1"})out.push_back(calc.read_register(name));
  for(const char name:std::string("0123456789abcde"))out.push_back(calc.read_register(std::string(1,name)));
  calc.press(".");out.push_back(calc.display_text(true));
  return out;
}
int cells(const std::vector<MachineItem>& m) {return static_cast<int>(std::count_if(m.begin(),m.end(),[](const auto& i){return i.kind!=MachineItemKind::Label;}));}
