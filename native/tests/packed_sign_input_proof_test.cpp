#include "unit_argument_test_support.hpp"
#include "mkpro/core/callee_hole_boundary_normalization.hpp"
#include "mkpro/core/post_layout_numeric_ranges.hpp"
#include "mkpro/core/post_layout_packed_sign.hpp"
#include <iostream>
#include <stdexcept>
using namespace mkpro;using namespace mkpro::core;
std::vector<MachineItem> fixture(bool,bool,bool,bool,bool);
std::vector<PreloadReport> preloads();
std::vector<passes::PassResult> candidates(const std::vector<MachineItem>&,const std::vector<PreloadReport>&);
int main() {try {
  const auto p=preloads();
  const auto choices=candidates(fixture(true,false,false,false,false),p);
  if(choices.empty())throw std::runtime_error("unit candidate");
  const auto items=lower_ir_to_machine(choices.front().ops);
  const auto flow=build_post_layout_control_flow(items);
  const auto lift=analyze_post_layout_automatic_entry_lift(items,flow,100000);
  if(!lift)throw std::runtime_error("entry proof");
  std::vector<std::size_t> queries,stores;
  for(std::size_t s=0;s<flow.execution_states.size();++s)if(items[flow.execution_states[s].item_index].opcode==0x32)queries.push_back(s);
  for(std::size_t i=0;i<items.size();++i)if(items[i].opcode==0x48)stores.push_back(i);
  const auto proof=prove_post_layout_packed_sign_inputs(items,flow,*lift,queries,&p,100000);
  const auto& signs=proof.signs;
  bool positive=false,negative=false;
  for(const auto& [s,v]:signs){(void)s;positive|=v==1;negative|=v==-1;}
  if(!positive||!negative)throw std::runtime_error("caller-context signed word facts");
  const auto numeric=prove_post_layout_numeric_ranges(items,flow,*lift,stores,&p);
  if(numeric.exhausted)throw std::runtime_error("numeric proof budget");
  for(const auto store:stores) {
    const auto at=numeric.x_before.find(store);
    if(at==numeric.x_before.end()||!at->second.integral||at->second.lower!=-1||at->second.upper!=1)
      throw std::runtime_error("sign certificate did not survive numeric fixed point");
  }
  for (const std::size_t budget : {0U, 1U}) {
    const auto bounded = prove_post_layout_packed_sign_inputs(
        items, flow, *lift, queries, &p, budget);
    if (!bounded.exhausted || !bounded.signs.empty() || bounded.work > budget)
      throw std::runtime_error("partial sign certificate escaped budget");
  }
  auto duplicates = p; duplicates.push_back(p.front());
  if (!prove_post_layout_packed_sign_inputs(items, flow, *lift, queries, &duplicates).signs.empty())
    throw std::runtime_error("duplicate preload identities accepted");
  auto extended = p; extended.push_back({.register_name="f", .value="0"});
  if (!prove_post_layout_packed_sign_inputs(items, flow, *lift, queries, &extended).signs.empty())
    throw std::runtime_error("stock proof accepted RF");
  std::vector<MachineItem> opaque{MachineItem::op(0x61,"load"),MachineItem::op(0x32,"sign"),MachineItem::op(0x50,"stop")};
  opaque.back().stop_disposition=StopDisposition::Terminal;
  opaque[1].roles.push_back(std::string(kUnitProjectionDemandRole));
  const auto rawflow=build_post_layout_control_flow(opaque);
  const auto rawlift=analyze_post_layout_automatic_entry_lift(opaque,rawflow,100000);
  const std::vector<PreloadReport> raw{{.register_name="1",.value="FA"}};
  if(!rawlift||prove_post_layout_numeric_ranges(opaque,rawflow,*rawlift,{2},&raw).x_before.contains(2))
    throw std::runtime_error("super-number sign must remain unknown");
  std::cout<<"packed sign/numeric proof transport contracts passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
